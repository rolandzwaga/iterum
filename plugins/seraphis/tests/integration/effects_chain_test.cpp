// ==============================================================================
// Seraphis - Effects chain order and send topology (Phase 10)
// ==============================================================================
// Reference: specs/seraphis-phase10-effects/spec.md   (C-1, FR-021)
//            specs/seraphis-phase10-effects/plan.md   (D-2, R-2; tasks.md T009)
//
// CRITERIA OWNED BY THIS TU: spec C-1's chain order and the requirements that
// hang off it. This file is created by T009 with ONE section - the D-2
// single-writer regression - and later Phase 10 tasks add their sections to the
// same TEST_CASE. Landed so far:
//   T009  SECTION "ID 1400 is the only writer on output saturation"   (D-2)
//   T011  SECTION "the pre-output tap is honest about truncation"     (FR-040,
//         FR-041 clause 6, plan D-8 clause 3), plus the FR-040 probe DEFINITION
//         and TEST_CASE "Effects defaults are a no-op on the same build" (SC-002)
//   T013  TEST_CASE "The effects send is block-size invariant"        (SC-017,
//         FR-003a) with its MANDATORY negative control, plus SECTIONs
//         "the send carries no current-block dry"                     (FR-004)
//         and "the drain's energy floor ends the window early"        (FR-009a)
//   T014  TEST_CASE "Effects parameters reach their components"       (FR-022,
//         FR-016a, FR-025, FR-023)
//   T015  TEST_CASE "Spectral delay decays at registered max feedback" (SC-005)
//         and TEST_CASE "Synced delay tracks host tempo"               (SC-019)
//   T016  TEST_CASE "Spectral freeze holds the Aether tail"            (SC-007,
//         FR-023a, plan D-5) and TEST_CASE "A short mix excursion preserves the
//         send tail"                                          (SC-011a, FR-009a)
//   T017  SC-003's three clauses, as SECTIONs of the case above -
//         "step 5 precedes step 6" (with its MANDATORY positive control),
//         "step 4 follows step 3" and "step 4 precedes step 5" - plus the four
//         wander SECTIONs "azimuth is unity at centre" (FR-010, plan D-4),
//         "the azimuth pan pair is evaluated on the 64-sample grid" (FR-024),
//         "depth never reaches BrownianDrift::setDepth" (FR-024a) and
//         "the wander disengage does not step the image" (FR-010a)
//   T018  TEST_CASE "Effects at maxima respect the true-peak ceiling"  (SC-006)
//         and TEST_CASE "Effects renders are seed-deterministic"       (SC-010,
//         FR-026, FR-027), the latter carrying the SALT-SWAP CONTROL and the
//         probe seam it needs
//   T019  TEST_CASE "Effects transitions are click-free"               (SC-008,
//         FR-008, FR-009, FR-010a, plan D-5), with BOTH mandatory positive
//         controls - the detector-wiring injection and the capability-3
//         return-ramp snap
//
// WHY THE SINGLE-WRITER SECTION EXISTS (plan D-2, RULED 2026-08-02).
// SeraphisEngine::setOutputSaturation (seraphis_engine.h:672) had TWO on-change
// writers before this phase: the prepare-time push (processor.cpp:568-578) and
// pushGlobalParams()' kSoftLimitId block. Two independent on-change trackers on
// one setter is last-writer-wins with NO convergence: set ID 1400 to 0.8, then
// toggle ID 2 off and on again, and the gate's tracker re-pushes the literal
// kOutputSaturation = 0.15f (seraphis_engine.h:248) while ID 1400 still reads
// 0.8 - so the engine stays at 0.15 until ID 1400 next MOVES. Clause 3 below is
// exactly that sequence, and it is the assertion the shipped two-writer code
// fails. Phase 10 makes pushEffectsParams() (processor.cpp:1207-1217) the sole
// writer over the composed value `soft ? saturation : 0.0f`, with ID 2 keeping
// its shipped meaning as the GATE and ID 1400 supplying the amount that gate
// passes.
//
// THE READ-BACK IS SeraphisEngine::getOutputSaturation() (seraphis_engine.h:695)
// - a pure const forwarder to TapeSaturator::getSaturation(), i.e. "the amount
// last pushed", NOT the saturator's ramp position. That is why a single block is
// enough to observe each push and no settle window is needed.
//
// NO std::isnan / std::isinf / std::numeric_limits<>::infinity() ANYWHERE: the
// macOS leg builds with -ffast-math, under which the compiler may assume finite
// values and fold such a test away.
//
// NO CHECKED-IN FLOAT GOLDEN. Every value asserted here is either a parameter
// amount pushed in this same process, or a DIFFERENCE between two renders made
// by this same binary in this same process. SC-002's `max |a - b| == 0.0f` is an
// exact comparison but not a golden: it compares two runs of one compiled path,
// so it demands nothing of cross-toolchain FP reproducibility (the thing
// render_fingerprint.h:20-30 measures at 2.9e-5 and the project forbids pinning).
//
// COMPILE FLAGS: this TU IS listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt (T010,
//   CMakeLists.txt:105) - it checks non-finite payloads by bit pattern and
//   measures per-sample statistics that fast-math contraction would reshape.
//   integration/effects_perf_test.cpp deliberately stays OUT of that block.
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "parameters/effects_params.h"
#include "plugin_ids.h"

#include <render_fingerprint.h>

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/effects/spectral_delay.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/midside_processor.h>  // T017: the global width stage
#include <krate/dsp/systems/seraphis_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// =============================================================================
// FR-040 (as amended by plan D-7) - the THREE-CAPABILITY probe.
//
// processor.h DECLARES `Seraphis::detail::SeraphisEffectsStageBypassProbe` and
// friends it; this TU is the ONLY place in the repository that DEFINES it, so a
// shipping build has no way to call it and all three branches it drives are
// false on every shipping path.
//
// WHY THREE AND NOT ONE. FR-040 originally called the runtime skip its "sole
// capability", but two of this spec's own success criteria MANDATE the other
// two - SC-003(a)'s positive control needs C-1 step 5 run AFTER step 6, and
// SC-008's positive control (b) needs the 20 ms return-gain ramp snapped - so a
// one-capability probe leaves two mandatory controls unwritable.
//
// FR-039 is why these are RUNTIME flags rather than a compile-time switch:
// processor.cpp is compiled ONCE into seraphis_tests (tests/CMakeLists.txt), so
// a second `#if`-ed variant of the render path cannot coexist in this binary,
// and SC-002/SC-012 are therefore same-binary runtime comparisons.
// =============================================================================
namespace Seraphis::detail {

struct SeraphisEffectsStageBypassProbe {
    /// Capability 1 - renderSlice() skips C-1 steps 4 and 5 (SC-002, SC-012).
    static void setStageBypassed(Seraphis::Processor& processor, bool on) noexcept {
        processor.effectsStageBypassed_ = on;
    }
    /// Capability 2 - C-1 step 5 runs AFTER step 6 (SC-003(a)'s positive control).
    static void setStageAfterOutput(Seraphis::Processor& processor, bool on) noexcept {
        processor.effectsStageAfterOutput_ = on;
    }
    /// Capability 3 - the FR-008/FR-009 return-gain ramp snaps to instant
    /// (SC-008's positive control (b)). Consumed by the send stage's engage ramp.
    static void setReturnRampSnap(Seraphis::Processor& processor, bool on) noexcept {
        processor.effectsReturnRampSnap_ = on;
    }

    // -------------------------------------------------------------------------
    // T014 - READ-ONLY seams onto the three components pushEffectsParams() drives
    // -------------------------------------------------------------------------
    // FR-022/FR-025 are assertions about what the PROCESSOR PUSHED, and the only
    // honest witness of that is the component's own getter. These are deliberately
    // NOT new public `*ForTest()` accessors: FR-041's surface set is closed at
    // seven read surfaces plus the truncation flag, and this TU already holds the
    // one friendship the phase grants - so the seam costs the shipping header
    // nothing and cannot be reached from any other translation unit.
    [[nodiscard]] static const Krate::DSP::SpectralDelay& spectralDelay(
        const Seraphis::Processor& processor) noexcept {
        return processor.spectralDelay_;
    }
    [[nodiscard]] static const Krate::DSP::BrownianDrift& widthDrift(
        const Seraphis::Processor& processor) noexcept {
        return processor.widthDrift_;
    }
    [[nodiscard]] static const Krate::DSP::BrownianDrift& azimuthDrift(
        const Seraphis::Processor& processor) noexcept {
        return processor.azimuthDrift_;
    }

    // -------------------------------------------------------------------------
    // T017 - READ-ONLY seams onto C-1 step 5 (the wander)
    // -------------------------------------------------------------------------
    // Same rule as the three above: FR-041's PUBLIC surface set is closed at
    // seven read surfaces plus the truncation flag, so nothing here becomes a
    // `*ForTest()` accessor. This TU already holds the one friendship the phase
    // grants, so the seams cost the shipping header nothing and no other
    // translation unit can reach them.
    [[nodiscard]] static const Krate::DSP::MidSideProcessor& midSide(
        const Seraphis::Processor& processor) noexcept {
        return processor.globalMs_;
    }
    /// FR-010's RAW predicate, on the unsmoothed atomics. Its whole point is
    /// that it can be answered on the block the host wrote the value, which is
    /// what FR-024a's plugin-side depth multiply buys and what a depth pushed
    /// through BrownianDrift::setDepth() (150 ms output smoother) could not.
    [[nodiscard]] static bool wanderRunsRaw(const Seraphis::Processor& processor) noexcept {
        return processor.fxWanderRuns_;
    }
    /// FR-010a's LATCHED predicate - the one runWanderStage() actually skips on.
    [[nodiscard]] static bool wanderRunsEffective(const Seraphis::Processor& processor) noexcept {
        return processor.fxWanderRunsEffective_;
    }
    /// FR-010a's disengage window, in samples, as setupProcessing() derived it.
    [[nodiscard]] static std::int64_t wanderSettleSamples(
        const Seraphis::Processor& processor) noexcept {
        return processor.fxWanderSettleSamples_;
    }

    // --- FR-024's control-grid witness ---------------------------------------
    /// Control evaluations during the MOST RECENT process() call. One per audio
    /// sub-chunk, counted where the sub-chunk's samples were processed.
    [[nodiscard]] static std::size_t wanderControlUpdates(
        const Seraphis::Processor& processor) noexcept {
        return processor.wanderControlUpdates_;
    }
    [[nodiscard]] static std::span<const float> azimuthTargetsL(
        const Seraphis::Processor& processor) noexcept {
        return {
            processor.wanderAzimuthTargetL_.data(),
            std::min(processor.wanderControlUpdates_,
                     Seraphis::Processor::kWanderControlLogCapacity)};
    }
    [[nodiscard]] static std::span<const float> azimuthTargetsR(
        const Seraphis::Processor& processor) noexcept {
        return {
            processor.wanderAzimuthTargetR_.data(),
            std::min(processor.wanderControlUpdates_,
                     Seraphis::Processor::kWanderControlLogCapacity)};
    }
    [[nodiscard]] static std::span<const std::uint16_t> wanderChunkLengths(
        const Seraphis::Processor& processor) noexcept {
        return {
            processor.wanderChunkLengths_.data(),
            std::min(processor.wanderControlUpdates_,
                     Seraphis::Processor::kWanderControlLogCapacity)};
    }

    // -------------------------------------------------------------------------
    // T018 - SC-010 clause (ii): THE SALT-SWAP CONTROL
    // -------------------------------------------------------------------------
    // C-5 / FR-024a clause 3 forbids the two drifts sharing one stream: identical
    // salts would make width and azimuth walk in LOCKSTEP off the one seed, which
    // reads as a single moving object rather than as two independent ones. That
    // prohibition is unobservable unless the salts can be exchanged, and
    // kFxWidthDriftSalt / kFxAzimuthDriftSalt are `inline constexpr`
    // (processor.h:222-223) - a test cannot rebind them.
    //
    // So the control re-seeds the two members with each other's salt, which is
    // EXACTLY what the shipped burst at processor.cpp:1631-1634 would have written
    // had the two constants been swapped: `setSeed(seed ^ salt)` then `reset()`,
    // in that order, per brownian_drift.h:145 / :133 (reset() rewinds to the exact
    // post-prepare state, RNG included, so the seeded stream is what the walk
    // actually replays).
    //
    // IT MUST BE CALLED AFTER THE SEED BURST HAS RUN, not before: the burst fires
    // on change of kSeedId (processor.cpp:1621) and would overwrite the swap. Once
    // lastPushedFxSeedIndex_ has settled, nothing re-seeds the drifts again for
    // the rest of the render, so a post-burst swap sticks.
    //
    // Not a `*ForTest()` accessor, and deliberately so: FR-041's PUBLIC surface
    // set is closed at seven read surfaces plus the truncation flag, and this TU
    // already holds the one friendship the phase grants - so the seam costs the
    // shipping header nothing and no other translation unit can reach it.
    static void swapDriftSalts(Seraphis::Processor& processor, std::uint32_t seed) noexcept {
        processor.widthDrift_.setSeed(seed ^ kFxAzimuthDriftSalt);  // brownian_drift.h:145
        processor.widthDrift_.reset();                              // :133
        processor.azimuthDrift_.setSeed(seed ^ kFxWidthDriftSalt);
        processor.azimuthDrift_.reset();
    }
};

}  // namespace Seraphis::detail

namespace {

using Fixture = SeraphisTest::ProcessorFixture;
using Probe = Seraphis::detail::SeraphisEffectsStageBypassProbe;

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;
constexpr std::size_t kBlockSamples = 512;

/// RAII, and that is not decoration: a failed assertion inside a SECTION unwinds
/// out of the case, and a probe mode left set would silently reconfigure every
/// case that ran after it in the same binary.
class ScopedEffectsProbe {
public:
    ScopedEffectsProbe(Seraphis::Processor& processor, bool bypassed, bool afterOutput,
                       bool rampSnap) noexcept
        : proc_(&processor) {
        Probe::setStageBypassed(processor, bypassed);
        Probe::setStageAfterOutput(processor, afterOutput);
        Probe::setReturnRampSnap(processor, rampSnap);
    }
    ~ScopedEffectsProbe() noexcept {
        Probe::setStageBypassed(*proc_, false);
        Probe::setStageAfterOutput(*proc_, false);
        Probe::setReturnRampSnap(*proc_, false);
    }

    ScopedEffectsProbe(const ScopedEffectsProbe&) = delete;
    ScopedEffectsProbe& operator=(const ScopedEffectsProbe&) = delete;
    ScopedEffectsProbe(ScopedEffectsProbe&&) = delete;
    ScopedEffectsProbe& operator=(ScopedEffectsProbe&&) = delete;

private:
    Seraphis::Processor* proc_;
};

/// spec C-6 / plan D-2. ID 1400's registered default IS the engine's own shipped
/// constant, which is the whole reason the composed single writer is
/// bit-identical to Phase 9 at the defaults (spec SC-002). Pinned at compile
/// time so clause 4 below never has to transcribe 0.15f as a literal.
static_assert(Seraphis::kFxSaturationDefault == Krate::DSP::SeraphisEngine::kOutputSaturation,
              "spec C-6 / plan D-2: kFxSaturationDefault (effects_params.h:104) must BE "
              "SeraphisEngine::kOutputSaturation (seraphis_engine.h:248)");

/// The amount clause 1 drives ID 1400 to. ID 1400 is a UNIT parameter - its
/// denormalization row is `unit`, a plain clamp of the normalized value
/// (effects_params.h handleEffectsParamChange, case kFxSaturationId) - so the
/// normalized value and the pushed amount are the SAME number. The expected
/// value is therefore DERIVED from the driven one rather than transcribed, so
/// the two can never drift apart.
constexpr double kSaturationNormalized = 0.8;
constexpr float kSaturationPlain = static_cast<float>(kSaturationNormalized);

// -----------------------------------------------------------------------------
// FR-041 clause 6 - the pre-output tap
// -----------------------------------------------------------------------------
/// The tap buffers are pinned to the SAME constant the scratch is (FR-028), not
/// to the host block - which is exactly why the truncation flag has to exist.
constexpr std::size_t kMaxTapSamples = Krate::DSP::SeraphisEngine::kMaxBlockSamples;
static_assert(kMaxTapSamples == 2048u,
              "FR-041 clause 6 pins the tap to SeraphisEngine::kMaxBlockSamples (2048)");

/// Larger than the tap, and larger than the slice loop's own 2048 cap - so this
/// is the branch processor.cpp documents as "a host block larger than 2048".
constexpr Steinberg::int32 kOversizeBlock = 4096;

/// TruePeakLimiter's shipped ceiling: kDefaultCeilingDb = -1.0f => ceilingLin_ =
/// 0.8912509f (true_peak_limiter.h:46, :168). Same literal as
/// param_flow_test.cpp:63 and processor_audio_test.cpp:150.
constexpr float kLimiterCeilingLin = 0.8912509f;

/// The clause-3 difference floor. The tap is read BEFORE TapeSaturator +
/// TruePeakLimiter, so once the limiter is provably in gain reduction the two
/// must differ by far more than this.
constexpr float kTapVsOutputFloor = 1.0e-3f;

/// How long clause 3 is willing to build level for, in 512-sample blocks
/// (2250 x 512 / 48 000 = 24 s). MEASURED PRECEDENT, not a guess:
/// param_flow_test.cpp's SC-027 drive ladder records that 16 notes at velocity
/// 1.0, master gain x2 and polyphony 16 reach a pre-output-stage peak of
/// 0.891251 - the limiter ceiling itself - only after 16 s of held chord
/// (param_flow_test.cpp:369, rung L4), because the level is built by the Aether
/// tail rather than by the attack. The loop below breaks the moment the
/// precondition is met, so this bound is only paid if it never is.
constexpr std::size_t kLoudMaxBlocks = 2250;

/// SC-002's script: eight voices - the shipped polyphony default
/// (global_params.h:44) - held for most of the render and released before its
/// end, so both the sustained and the released halves of the chain are compared.
constexpr Steinberg::int16 kEightNoteChord[] = {48, 52, 55, 59, 62, 64, 67, 71};
constexpr float kChordVelocity = 0.8f;

/// Clause 3's chord: sixteen voices, the same shape param_flow_test.cpp's L4
/// rung was measured with.
constexpr Steinberg::int16 kSixteenNoteChord[] = {36, 40, 43, 47, 48, 52, 55, 59,
                                                  60, 64, 67, 71, 72, 76, 79, 83};

/// 938 x 512 = 480 256 samples = 10.005 s at 48 kHz - SC-002's "10 s".
constexpr std::size_t kNoOpRenderBlocks = 938;
/// Note-offs at 7.47 s, leaving ~2.5 s of release and tail inside the render.
constexpr std::size_t kNoOpNoteOffBlock = 700;

[[nodiscard]] float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    }
    return worst;
}

/// SC-002 is "ONE build, ONE process, ONE Processor instance", so the two renders
/// cannot be two fixtures - but they must start from identical DSP state, and the
/// first render leaves a live reverb tail, voice allocator and smoother set
/// behind. Re-preparing the SAME instance is what reconciles the two: setActive
/// (false) silences the engine and clears the reverb tail (processor.cpp:644-654)
/// and setupProcessing() re-prepares both components, re-seeds the spectral slots
/// from the current atomics and raises pushAllSurfaces(Reprepared)'s snap.
///
/// WHAT IT DOES *NOT* DO - AND WHY THE WARM-UP RENDER BELOW EXISTS. A re-prepare
/// does NOT rewind the instance to a VIRGIN one. `ContinuousBody` is path
/// dependent between its first render and every later one, and a full prepare()
/// does not close that gap either - measured, and documented at the layer that
/// owns it: dsp/tests/unit/systems/seraphis_voice_test.cpp:816-827 ("A FULL
/// prepare() DOES NOT CLOSE IT EITHER (measured identical, 1.886e-5 max, so it is
/// not something reset() forgot)"). MEASURED HERE, through the whole processor,
/// on this exact 10 s script: virgin-vs-re-prepared diverges from the first
/// audible sample (index 1024, the spectral stage's own latency) by 6.5e-3, while
/// two fresh Processor instances agree at 0.0f and the SECOND and THIRD renders of
/// one re-prepared instance also agree at 0.0f. The residue is therefore a FIXED
/// POINT reached after one render, which is exactly what the warm-up buys.
void reprepare(Fixture& fx) {
    REQUIRE(fx.proc->setActive(false) == Steinberg::kResultOk);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate = kSampleRate;

    REQUIRE(fx.proc->setupProcessing(setup) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(true) == Steinberg::kResultOk);
}

// =============================================================================
// T013 - SC-017: PARTITION INVARIANCE WITH THE SEND ACTIVE
// =============================================================================
/// 4 s at 48 kHz - the same geometry Phase 9's own partition sweep uses
/// (unit/midi_event_test.cpp:115-117), and long enough that the spectral stage's
/// 1024-sample latency, the accumulator's fixed 512-sample pipeline delay and
/// sixteen traversals of the 250 ms delay are all fully inside the compared
/// region rather than sitting in a start-up transient.
constexpr std::size_t kInvarianceTotalSamples = 192000;

/// THE REFERENCE PARTITION, and why the criterion's "single contiguous render"
/// is realised as a 512-sample one rather than as one 192 000-sample call: the
/// shipped SC-008 sweep (midi_event_test.cpp:119-121) establishes exactly this
/// shape, 512 is itself one of the partitions SC-017 requires to agree, and a
/// render is only "contiguous" from the SEND's point of view when nothing
/// re-phases the accumulator - which is a property of the parameter script (no
/// bypass, freeze or seed transition), not of the host block length. The
/// reference is NOT compared with itself; that comparison carries no
/// information.
constexpr std::size_t kInvarianceReferenceBlock = 512;

/// SC-017's list, verbatim. 4096 is the only partition above
/// SeraphisEngine::kMaxBlockSamples = 2048, i.e. the only one that enters the
/// slice loop's cap branch, and 1/2/3/7 are all far below the 64-sample control
/// grid, so on those every send call is a small fraction of one chunk and the
/// accumulator has to carry state across many calls before it ever runs one.
/// BOTH facts are asserted in the case below, not assumed.
///
/// (This case compares PLUGIN OUTPUT through render_fingerprint.h, not the tap,
/// so the 4096 block is legal here - unlike every tap-measuring criterion, which
/// must assert preOutputTapTruncatedForTest() false and therefore stay <= 2048.)
constexpr std::size_t kInvariancePartitions[] = {1, 2, 3, 7, 512, 2048, 4096};

// --- SC-017's operating point, every value DERIVED from the shipped range ----
constexpr double kInvMixNormalized = 1.0;  // kFxDelayMixId is a plain unit row
constexpr float kInvFeedbackPlain = 0.6f;
constexpr double kInvFeedbackNormalized = static_cast<double>(kInvFeedbackPlain)
                                          / static_cast<double>(Seraphis::kFxDelayFeedbackMax);
constexpr float kInvDelayMsPlain = 250.0f;
constexpr double kInvDelayNormalized =
    static_cast<double>(kInvDelayMsPlain - Seraphis::kFxDelayTimeMinMs)
    / static_cast<double>(Seraphis::kFxDelayTimeMaxMs - Seraphis::kFxDelayTimeMinMs);
/// "The wander engaged". Both depth rows are plain unit rows, and both are
/// class-(b) smoothed (FR-038b clause 2), so setting them is also what forces
/// the 64-sample sub-slice subdivision (plan D-6) into the first 20 ms of every
/// partition - the branch a partition-dependent implementation is most likely to
/// get wrong.
constexpr double kInvWanderDepthNormalized = 0.7;
constexpr double kInvAzimuthDepthNormalized = 0.4;

struct SendScriptEvent {
    std::size_t position;
    bool noteOn;
    Steinberg::int16 pitch;
    float velocity;
};

/// Every position is odd and divisible by neither 3 nor 7, so it is a
/// non-multiple of every partition above 1 and no event ever lands on a block
/// boundary in any of them (ASSERTED in the case, not assumed). Without that,
/// the MIDI sub-division path would go unexercised for whichever partition an
/// event happened to align with.
///
/// The script carries NO parameter automation after block 0. VST3 delivers
/// parameter queues per HOST BLOCK, so a mid-render automation point is a
/// different lane in every partition by construction - which is also why SC-017
/// requires the render to be transition-free.
constexpr SendScriptEvent kSendScript[] = {
    {.position = 1003, .noteOn = true, .pitch = 48, .velocity = 0.80f},
    {.position = 13337, .noteOn = true, .pitch = 55, .velocity = 0.60f},
    {.position = 40001, .noteOn = true, .pitch = 60, .velocity = 0.90f},
    {.position = 71111, .noteOn = false, .pitch = 48, .velocity = 0.0f},
    {.position = 99991, .noteOn = true, .pitch = 67, .velocity = 0.50f},
    {.position = 133333, .noteOn = false, .pitch = 55, .velocity = 0.0f},
    {.position = 160007, .noteOn = false, .pitch = 60, .velocity = 0.0f},
};

struct SendRender {
    std::vector<float> left, right;
};

/// One complete 4 s render through Processor::process() at `blockSize`, with the
/// send active throughout. The fixture (and the ~33 MB of per-voice capture
/// rings the engine allocates at prepare) is destroyed when this returns, so at
/// most one engine is alive at a time even though seven partitions are rendered.
[[nodiscard]] SendRender renderSendAtBlockSize(std::size_t blockSize) {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, static_cast<Steinberg::int32>(blockSize))
            == Steinberg::kResultOk);

    // ceil: a partition that does not divide the total renders one extra block,
    // and the comparison then truncates. The render is causal, so a trailing
    // partial block cannot influence what is compared.
    const std::size_t numBlocks = (kInvarianceTotalSamples + blockSize - 1u) / blockSize;

    fx.renderBlocks(numBlocks, blockSize,
                    [blockSize](std::size_t b, Krate::Test::EventList& events,
                                SeraphisTest::ParameterChanges& params) {
                        if (b == 0) {
                            // All five land at sample offset 0 of block 0, i.e.
                            // BEFORE any audio is rendered in EVERY partition -
                            // which is what keeps the operating point itself
                            // partition-independent.
                            params.addQueue(Seraphis::kFxDelayMixId)
                                .addTestPoint(0, kInvMixNormalized);
                            params.addQueue(Seraphis::kFxDelayFeedbackId)
                                .addTestPoint(0, kInvFeedbackNormalized);
                            params.addQueue(Seraphis::kFxDelayTimeId)
                                .addTestPoint(0, kInvDelayNormalized);
                            params.addQueue(Seraphis::kFxWanderDepthId)
                                .addTestPoint(0, kInvWanderDepthNormalized);
                            params.addQueue(Seraphis::kFxAzimuthDepthId)
                                .addTestPoint(0, kInvAzimuthDepthNormalized);
                        }
                        const std::size_t blockStart = b * blockSize;
                        for (const SendScriptEvent& e : kSendScript) {
                            if (e.position < blockStart || e.position >= blockStart + blockSize) {
                                continue;
                            }
                            const auto offset =
                                static_cast<Steinberg::int32>(e.position - blockStart);
                            if (e.noteOn) {
                                events.addNoteOn(e.pitch, e.velocity, offset);
                            } else {
                                events.addNoteOff(e.pitch, offset);
                            }
                        }
                    });

    REQUIRE(fx.capturedL.size() >= kInvarianceTotalSamples);
    REQUIRE(fx.capturedR.size() >= kInvarianceTotalSamples);
    REQUIRE(fx.checkCanaries());

    SendRender out;
    out.left.assign(fx.capturedL.begin(),
                    fx.capturedL.begin() + static_cast<std::ptrdiff_t>(kInvarianceTotalSamples));
    out.right.assign(fx.capturedR.begin(),
                     fx.capturedR.begin() + static_cast<std::ptrdiff_t>(kInvarianceTotalSamples));
    return out;
}

// -----------------------------------------------------------------------------
// SC-017's MANDATORY NEGATIVE CONTROL
// -----------------------------------------------------------------------------
// The criterion means nothing unless partition dependence is REAL for a send
// that skips the accumulator. It is deliberately NOT measured by adding a fourth
// runtime capability to FR-040's probe that fed SpectralDelay::process a raw
// slice length: FR-003a forbids that call shape outright, so a branch performing
// it would put the prohibited call into the shipping audio-thread binary purely
// so a test could take it. The control therefore drives the COMPONENT directly -
// which is exactly where the defect lives (spec Overview fact 5): STFT::
// canAnalyze() needs fftSize_ samples (stft.h:134-137), analyze() consumes
// hopSize_ (:171), synthesize() marks samplesReady_ += hopSize_ (:311), and
// process() pulls min(numSamples, availableL, availableR) (spectral_delay.h:366)
// while writing dryBuffer * dryMix - silence at 100 % wet - into whatever it
// cannot supply (:383-386). A single 2048-sample call has three analyses ready
// and lands wet-stream sample 0 at output index 0; the same audio as four
// 512-blocks lands it at index 512, PERMANENTLY.

/// Big enough for the largest call in the ragged partition. SpectralDelay::
/// process copies numSamples into dryBufferL_ (spectral_delay.h:341), which is
/// sized by prepare()'s maxBlockSize, so this bound is load-bearing.
constexpr std::size_t kRawSendMaxBlock = 4096;
/// 1 s at 48 kHz - four traversals of the 250 ms delay, so the divergence is a
/// steady-state property and not a start-up artefact.
constexpr std::size_t kRawSendSamples = 48000;
constexpr std::uint32_t kRawSendRngSeed = 0x5E11A013u;
constexpr std::uint32_t kRawSendNoiseSeed = 0x13579BDFu;
/// The control's OWN reference: the accumulator's call shape, i.e. a constant
/// kFxSendChunkSamples. It is what the plugin actually does, so the divergence
/// measured below is exactly "what the raw slice lengths cost".
constexpr std::size_t kRawSendConstantCall[] = {Seraphis::kFxSendChunkSamples};

/// A 32-bit LCG, in INTEGER arithmetic only. The excitation must be identical
/// sample-for-sample across the two partitions or the comparison would measure
/// the generator rather than the component - and it must not be reshaped by
/// -ffast-math, which rules out anything transcendental.
class TestNoise {
public:
    explicit TestNoise(std::uint32_t seed) noexcept : state_(seed) {}
    [[nodiscard]] float next() noexcept {
        state_ = state_ * 1664525u + 1013904223u;
        // Top 23 bits -> [0, 1), then mapped to [-1, 1).
        const float u = static_cast<float>(state_ >> 9u) * (1.0f / 8388608.0f);
        return 2.0f * u - 1.0f;
    }

private:
    std::uint32_t state_;
};

struct RawSendRender {
    std::vector<float> left, right;
};

/// Render `kRawSendSamples` of the SAME noise through a bare SpectralDelay,
/// cycling `callLengths` as the call partition.
[[nodiscard]] RawSendRender renderRawSend(std::span<const std::size_t> callLengths) {
    Krate::DSP::SpectralDelay delay;
    // The C-2 clause 1 order the processor itself uses: setDryWetMix BEFORE
    // prepare(), because prepare() ends in snapParameters() while a post-prepare
    // push only moves a smoother target.
    delay.setFFTSize(Krate::DSP::SpectralDelay::kDefaultFFTSize);
    delay.setDryWetMix(1.0f);
    delay.prepare(kSampleRate, kRawSendMaxBlock);
    delay.seedRng(kRawSendRngSeed);
    delay.reset();
    delay.setBaseDelayMs(kInvDelayMsPlain);
    delay.setFeedback(kInvFeedbackPlain);
    delay.snapParameters();

    const Krate::DSP::BlockContext ctx{.sampleRate = kSampleRate,
                                       .blockSize = kRawSendMaxBlock,
                                       .tempoBPM = 120.0,
                                       .isPlaying = false};

    RawSendRender out;
    out.left.assign(kRawSendSamples, 0.0f);
    out.right.assign(kRawSendSamples, 0.0f);

    std::vector<float> scratchL(kRawSendMaxBlock, 0.0f);
    std::vector<float> scratchR(kRawSendMaxBlock, 0.0f);
    TestNoise noise(kRawSendNoiseSeed);

    std::size_t cursor = 0;
    std::size_t k = 0;
    while (cursor < kRawSendSamples) {
        const std::size_t n =
            std::min(callLengths[k % callLengths.size()], kRawSendSamples - cursor);
        ++k;
        for (std::size_t i = 0; i < n; ++i) {
            const float s = noise.next();
            scratchL[i] = s;
            // Decorrelated but still deterministic, so the stereo halves of the
            // component are both exercised.
            scratchR[i] = noise.next();
        }
        delay.process(scratchL.data(), scratchR.data(), n, ctx);
        std::copy_n(scratchL.data(), n, out.left.data() + cursor);
        std::copy_n(scratchR.data(), n, out.right.data() + cursor);
        cursor += n;
    }
    return out;
}

// =============================================================================
// T013 - FR-004 (the send carries no current-block dry)
// =============================================================================
/// The block on which kFxDelayMixId steps 0 -> 1. Late enough that the bus is
/// already loud (the reported latency is 1024 samples, so nothing is audible
/// before it) and early enough that the whole measured window fits in the
/// render. It is ALSO inside kFxSendDrainMs of the render start, so FR-008's
/// deferred reset does NOT fire here - which keeps the accumulator in exactly
/// the state clearFifos() left at prepare and makes the expected silent window
/// a pure function of the pipeline, not of a reset that happened to land.
constexpr std::size_t kDryProbeEngageBlock = 4;
constexpr std::size_t kDryProbeEngageSample = kDryProbeEngageBlock * kBlockSamples;
/// FR-004's window: the component cannot emit anything derived from its input
/// until fftSize samples have been pushed, and the accumulator adds a fixed
/// one-chunk pipeline delay on top of that.
constexpr std::size_t kDryProbeSilentSamples =
    Krate::DSP::SpectralDelay::kDefaultFFTSize + Seraphis::kFxSendChunkSamples;
static_assert(kDryProbeSilentSamples == 1536u,
              "FR-004: fftSize (1024) + kFxSendChunkSamples (512)");
/// 24 576 samples. Covers the engage point, the 1536-sample silent window, the
/// 250 ms (12 000-sample) delay and a margin, so the LATE clause below can
/// observe the return actually arriving.
constexpr std::size_t kDryProbeBlocks = 48;
/// The isolated return in the silent window must be zero, not merely small. 1e-7
/// is four orders below the ~50 % dry a post-prepare setDryWetMix() push leaks,
/// and a correct implementation adds literal 0.0f there, so the measured value
/// is exactly 0.
constexpr float kDryProbeSilentFloor = 1.0e-7f;
/// Non-vacuity floors: the bus must be loud in the measured window, and the
/// return must actually arrive later on.
constexpr float kDryProbeBusFloor = 1.0e-3f;
constexpr float kDryProbeReturnFloor = 1.0e-3f;

// =============================================================================
// T013 - FR-009a's kFxSendDrainFloor (the drain's ENERGY exit)
// =============================================================================
// THIS IS THE ONLY PLACE THE FLOOR IS THE DISCRIMINATOR. At every other
// operating point the phase uses - 6.8 s at SC-011a's feedback 0.6, 3.3 s at the
// C-6 default 0.35 - the 2 s wall-clock cap fires FIRST, so an implementation
// that omitted the floor check entirely would pass SC-011a, SC-012, SC-013 and
// SC-018 unchanged. A low-feedback / short-delay point is what inverts that.
constexpr float kFloorFeedbackPlain = 0.1f;
constexpr double kFloorFeedbackNormalized =
    static_cast<double>(kFloorFeedbackPlain) / static_cast<double>(Seraphis::kFxDelayFeedbackMax);
constexpr float kFloorDelayMsPlain = 50.0f;
constexpr double kFloorDelayNormalized =
    static_cast<double>(kFloorDelayMsPlain - Seraphis::kFxDelayTimeMinMs)
    / static_cast<double>(Seraphis::kFxDelayTimeMaxMs - Seraphis::kFxDelayTimeMinMs);
/// ~1.07 s of live send before the bypass, so the component is genuinely full.
constexpr std::size_t kFloorBuildBlocks = 100;
/// ~0.78 s of drain. ln(1e-6)/ln(0.1) ~ 6 traversals of 50 ms ~ 0.3 s, so the
/// floor has fired well inside this - and it is an order of magnitude inside
/// kFxSendDrainMs = 2000.
constexpr std::size_t kFloorObserveBlocks = 73;
/// A further ~0.56 s, bringing the total drain to 178 blocks = 91 136 samples.
/// That is deliberately BELOW the 2 s (96 000-sample) cap, which the case
/// asserts: if the cap could have fired, the criterion would not be measuring
/// the floor at all.
constexpr std::size_t kFloorFreezeBlocks = 105;

// =============================================================================
// T014 - pushEffectsParams(): FR-022, FR-016a, FR-025, FR-023
// =============================================================================
// EVERY expected value below is DERIVED from the shipped range constants in
// effects_params.h, never transcribed: the denormalization row and the
// expectation would otherwise be two independent transcriptions of one mapping,
// and a wrong range would move both together and assert nothing.
//
// Every driven value is also deliberately DIFFERENT from the component's own
// construction default (spectral_delay.h:880-893: baseDelayMs_ = 250,
// spreadMs_ = 0, spreadDirection_ = LowToHigh, feedback_ = 0, feedbackTilt_ = 0,
// diffusion_ = 0, stereoWidth_ = 0, timeMode_ = Free, noteValueIndex_ = 4).
// Without that, a build that never called the setter at all would still report
// the expected value and the whole section would be vacuous - which is exactly
// the hole tasks.md T014 clause 1 names for setSpreadMs / setDiffusion /
// setStereoWidth / setSpreadDirection.

constexpr double kFxTimeNormalized = 0.5;
constexpr float kFxTimePlain =
    static_cast<float>(Seraphis::kFxDelayTimeMinMs
                       + kFxTimeNormalized
                             * (Seraphis::kFxDelayTimeMaxMs - Seraphis::kFxDelayTimeMinMs));
static_assert(kFxTimePlain != Krate::DSP::SpectralDelay::kDefaultDelayMs,
              "FR-022: the driven delay time must differ from the component default");

constexpr double kFxSpreadNormalized = 0.25;
constexpr float kFxSpreadPlain =
    static_cast<float>(Seraphis::kFxDelaySpreadMinMs
                       + kFxSpreadNormalized
                             * (Seraphis::kFxDelaySpreadMaxMs - Seraphis::kFxDelaySpreadMinMs));

/// Index 2 = SpreadDirection::CenterOut - the enumerator FR-017 exists to keep
/// reachable, and the one a two-entry table would have stranded.
constexpr int kFxDirectionIndex = 2;
constexpr double kFxDirectionNormalized =
    static_cast<double>(kFxDirectionIndex)
    / static_cast<double>(Seraphis::kFxSpreadDirectionLabels.size() - 1);

/// The registered MAXIMUM (C-7 clause 2 / FR-016), so the compensation below is
/// exercised at the only feedback setting where it is load-bearing.
constexpr double kFxFeedbackNormalized = 1.0;
constexpr float kFxFeedbackPlain = Seraphis::kFxDelayFeedbackMax;

/// -0.5, i.e. a tilt whose compensation divisor (1.5) is neither 1 nor 2 - so a
/// build that hard-coded either would fail here rather than pass by coincidence.
constexpr double kFxTiltNormalized = 0.25;
constexpr float kFxTiltPlain =
    static_cast<float>(Seraphis::kFxDelayTiltMin
                       + kFxTiltNormalized * (Seraphis::kFxDelayTiltMax - Seraphis::kFxDelayTiltMin));

constexpr double kFxDiffusionNormalized = 0.8;  // a plain unit row
constexpr double kFxDelayWidthNormalized = 0.9; // a plain unit row
constexpr double kFxWanderRateNormalized = 0.8; // a plain unit row
static_assert(static_cast<float>(kFxWanderRateNormalized)
                  != Krate::DSP::BrownianDrift::kDefaultSmoothness,
              "FR-025: the driven rate must differ from the prepared default");

/// Index 3 = "1/32T" in the FR-017 table. Deliberately NOT 4, which is the
/// component's own construction default (spectral_delay.h:893).
constexpr int kFxSyncNoteIndex = 3;
constexpr double kFxSyncNoteNormalized =
    static_cast<double>(kFxSyncNoteIndex)
    / static_cast<double>(Seraphis::kFxDelaySyncNoteLabels.size() - 1);

/// FR-016a's two worst-case tilts, and the value the helper must produce at the
/// registered maximum. effects_params.h already pins this at COMPILE time; the
/// runtime clause exists because a processor that ignored the helper and pushed
/// the raw feedback would satisfy that static_assert perfectly.
constexpr float kFxCompensatedAtFullTilt = 0.475f;

/// "On change only" - how many blocks of re-writing the SAME values must leave
/// effectsPushCountForTest() unmoved.
constexpr std::size_t kFxUnchangedBlocks = 10;

// --- FR-023: the two freezes are independent ---------------------------------
/// 48 000 / 512 = 93.75, rounded up - so every window below is at least the
/// nominal duration it is named for.
constexpr std::size_t kIndepBlocksPerSecond = 94;
constexpr std::size_t kIndepNoteOffBlock = kIndepBlocksPerSecond;                     // ~1 s
constexpr std::size_t kIndepEarlyBlock = kIndepNoteOffBlock + kIndepBlocksPerSecond;  // ~1 s later
constexpr std::size_t kIndepLateBlock =
    kIndepNoteOffBlock + 4u * kIndepBlocksPerSecond;  // ~4 s after note-off
constexpr std::size_t kIndepWindowBlocks = 47;        // ~0.5 s
constexpr std::size_t kIndepTotalBlocks = kIndepLateBlock + kIndepWindowBlocks;
/// >= 6 dB across the ~3 s between the two windows. The shipped Aether decay
/// default is 4.0 s (aether_params.h:53), i.e. ~45 dB over that span for an
/// UNfrozen reverb, so this is an order of magnitude of slack - while a FROZEN
/// one holds its energy and cannot meet it at all.
constexpr float kIndepDecayFactor = 0.5f;
/// ~40 blocks = 0.43 s, comfortably past AetherReverb's 50 ms freeze latch
/// (kFreezeLatchMs, aether_reverb.h:1388) - isFrozen() is deliberately false
/// until the latch COMPLETES (:2488-2495).
constexpr std::size_t kIndepLatchBlocks = 40;

[[nodiscard]] float windowRms(const std::vector<float>& samples, std::size_t firstBlock,
                              std::size_t numBlocks) {
    const std::size_t begin = firstBlock * kBlockSamples;
    const std::size_t end = std::min(samples.size(), begin + numBlocks * kBlockSamples);
    if (begin >= end) {
        return 0.0f;
    }
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(end - begin)));
}

// =============================================================================
// T017 - SC-003 (C-1's THREE load-bearing placements) AND THE WANDER STAGE
// =============================================================================
// SC-003's isolated send return has the definition every tap-measuring criterion
// in this phase inherits:
//
//   render(kFxDelayMixId = 1) - render(kFxDelayMixId = 0), same script, same
//   seed, read from preOutputTapForTest() - EXCEPT clause (a), which measures the
//   TRUE PLUGIN OUTPUT, because the limiter IS its subject and reading before the
//   limiter would make it vacuous.
//
// The shared operating point is spelled out in tasks.md T017: send at mix 1.0,
// delay time 0, feedback 0, wander at width 200 %.

/// kFxDelayTimeMinMs = 0 (spectral_delay.h:91), so normalized 0 IS "delay 0".
constexpr double kOrderDelayNormalized = 0.0;
constexpr double kOrderFeedbackNormalized = 0.0;
/// kFxWidthMaxPercent = 200 (midside_processor.h:66), the top of the registered
/// range - the setting at which the M/S stage's side scaling is most visible and
/// at which a step-5-after-step-6 order most obviously re-inflates the peak.
constexpr double kOrderWidthMaxNormalized = 1.0;
constexpr double kOrderWidthUnityNormalized = 0.5;   // -> exactly 100 %
constexpr double kOrderWidthMonoNormalized = 0.0;    // -> exactly 0 %
static_assert(Seraphis::kFxWidthMinPercent == 0.0f && Seraphis::kFxWidthMaxPercent == 200.0f,
              "T017: the width normalizations above assume the registered [0, 200] range");

/// SC-003(a). How many blocks are measured once the limiter is PROVABLY in gain
/// reduction. Long enough that the wander's slow walk moves the image across the
/// window (BrownianDrift's decorrelation time is 0.2-30 s), so the control arm's
/// post-limiter multiply is sampled at more than one azimuth position.
constexpr std::size_t kCeilingObserveBlocks = 94;  // ~1 s
/// The same allowance processor_audio_test.cpp:153 declares and applies at :810.
/// The linear bound is DERIVED from it in the section rather than transcribed,
/// so the two can never disagree.
constexpr float kCeilingAllowanceDb = 0.1f;

/// SC-003(b)/(c). ~1 s renders: the send's pipeline (1024 fftSize + 512 chunk)
/// and its 20 ms engage ramp are all inside the first ~2 blocks, so a window
/// that starts at block 20 measures a fully live send.
constexpr std::size_t kOrderRenderBlocks = 94;
constexpr std::size_t kOrderMeasureFirstBlock = 20;
constexpr std::size_t kOrderMeasureBlocks = kOrderRenderBlocks - kOrderMeasureFirstBlock;

/// SC-003(b)'s two master-gain settings. kMasterGainId is `normalized * 2.0`
/// (global_params.h:91-95), so these are linear 0.25 and 0.5 - EXACTLY a factor
/// of two, and both low enough that the precondition (output peak >= 3 dB under
/// the ceiling) holds at the louder one.
constexpr double kOrderGainLowNormalized = 0.125;
constexpr double kOrderGainHighNormalized = 0.25;
/// 20*log10(2). The assertion is +/- 0.1 dB around it.
constexpr double kOrderExpectedGainDeltaDb = 6.0206;
constexpr double kOrderGainToleranceDb = 0.1;
/// The (b) precondition: the output peak stays at least this far UNDER the
/// ceiling, so the render is provably outside the limiter's gain reduction and
/// clause (b)'s linearity argument is not being made through a nonlinearity.
constexpr float kOrderHeadroomDb = -3.0f;

/// SC-003(c)'s ideal: MidSideProcessor multiplies side by width/100
/// (midside_processor.h:135, :210), so 200 % carries EXACTLY 2x the side
/// AMPLITUDE of 100 % and 4x the side ENERGY. The assertion is taken in dB on
/// the amplitude ratio (sqrt of the measured energy ratio), so the ideal is
/// 20*log10(2) = 6.0206 dB - which is the same number the energy ratio gives on
/// its own scale, 10*log10(4). A build that imaged only the dry bus leaves the
/// isolated return's side unmoved and reports ~0 dB here.
constexpr double kOrderExpectedSideDeltaDb = 6.0206;
constexpr double kOrderSideToleranceDb = 0.5;

/// Non-vacuity floors. The isolated return has to be audible before any ratio
/// measured on it means anything.
constexpr double kOrderReturnRmsFloor = 1.0e-6;
constexpr double kOrderSideEnergyFloor = 1.0e-10;
/// SC-003(c)'s mono clause, as a RELATIVE bound rather than an exact zero. In
/// exact arithmetic the side of the isolated return at width 0 % IS zero - side
/// is multiplied by 0.0f, so L and R are reconstructed from mid alone and their
/// difference is 0.0f - and it is zero here too as long as the azimuth pair's
/// two gains are the same float. They are on every leg measured so far
/// (cos and sin of 0.5f*kHalfPi both round to 0x3F3504F3), but that is a libm
/// property, not a language guarantee, and a 1-ulp split would leave a residue
/// of ~1.5e-8 x the return's MID content. 1e-8 of the width-100 % side energy is
/// ~7 orders above that residue and ~15 orders below a real width leak.
constexpr double kOrderMonoSideRelativeBound = 1.0e-8;

// --- FR-010 / plan D-4: "azimuth is unity at centre" -------------------------
/// The epsilon that CROSSES FR-010's skip boundary without meaningfully moving
/// the image: position = 0.5 + 0.5 * eps * drift, and |drift| <= 1, so the pan
/// position stays within 5e-4 of centre while the raw predicate flips from
/// "skip" to "run". An uncompensated equalPowerGains pair drops the bus 3.01 dB
/// across exactly this step; the compensated one must not move it 0.1 dB.
constexpr double kAzimuthEpsilonNormalized = 1.0e-3;
constexpr double kAzimuthUnityToleranceDb = 0.1;
constexpr std::size_t kAzimuthRenderBlocks = 141;      // ~1.5 s
constexpr std::size_t kAzimuthMeasureFirstBlock = 30;  // past the 20 ms engage ramp

// --- FR-024: the 64-sample control grid --------------------------------------
/// ONE block of exactly kMaxBlockSamples, which is also the largest slice the
/// loop will ever cut (processor.cpp's 2048 cap) - so with every class-(b)
/// smoother settled this block is ONE slice, and the number of control
/// evaluations inside it is a direct measurement of the grid.
constexpr Steinberg::int32 kGridBlock = 2048;
constexpr std::size_t kGridBlockSamples = 2048;
static_assert(kGridBlockSamples == kMaxTapSamples,
              "FR-024's block is exactly kMaxBlockSamples, so the tap is not truncated");
constexpr std::size_t kGridControlChunk = Seraphis::kWanderControlChunkSamples;
static_assert(kGridControlChunk == 64u,
              "C-5/FR-024: the grid IS SeraphisEngine::kControlChunkSamples");
/// ceil(2048/64). The criterion's "at most 32 distinct target values".
constexpr std::size_t kGridExpectedUpdates = kGridBlockSamples / kGridControlChunk;
static_assert(kGridExpectedUpdates == 32u, "ceil(2048/64) = 32");
/// Blocks rendered before the measured one, so every class-(b) smoother has
/// settled and the measured block is a SINGLE 2048-sample slice rather than 32
/// sub-slices. 20 ms = 960 samples, so two blocks is already twice over.
constexpr std::size_t kGridSettleBlocks = 4;

// --- FR-024a: depth is a plugin-side multiply --------------------------------
/// ~1 s per leg of the 0 -> 1 -> 0 sweep. A whole second of LIVE wander, not a
/// fraction of one, because BrownianDrift starts its walk at the mean (0) and
/// the difference clause below needs the walk to have actually moved the image.
constexpr std::size_t kDepthLegBlocks = 94;
/// The live-wander difference floor. Well above the ~6e-8 relative residue a
/// stage that ran but never MOVED would leave, so the clause cannot be satisfied
/// by a wander that is engaged in name only.
constexpr float kDepthLiveDiffFloor = 1.0e-5f;
/// Blocks waited after the disengage write before the two renders are required
/// to be BIT-IDENTICAL again. fxWanderSettleSamples_ is 3 x 20 ms = 2880 samples
/// = 5.625 blocks; 20 blocks is ~3.5x that, so the assertion is about the skip
/// being EXACT and not about where the boundary sits.
constexpr std::size_t kDepthIdentityWaitBlocks = 20;
/// setupProcessing() derives this as llround(max(10, 20) ms * 3 * sr / 1000).
constexpr std::int64_t kExpectedWanderSettleSamples = 2880;

// --- FR-010a: the disengage must not step the image --------------------------
constexpr std::size_t kDisengageLiveBlocks = 94;       // ~1 s of live wander
constexpr std::size_t kDisengageWindowBlocks = 24;     // ~0.26 s, > the settle window
constexpr std::size_t kDisengageReferenceGap = 24;     // clear of the transition
constexpr std::size_t kDisengageTotalBlocks = kDisengageLiveBlocks + 2u * kDisengageWindowBlocks;
/// SC-008's own bound, reused: the transition window's worst per-sample delta
/// against a quiescent window of the SAME LENGTH from the SAME render.
constexpr float kDisengageDeltaBound = 1.5f;

/// DEFINED further down this TU (beside T015's isolated-return helpers) and
/// declared here because T017's sections precede it. The `-ffast-math` rule is
/// not waived by this TU's -fno-fast-math entry: std::isnan / std::isinf /
/// std::numeric_limits<>::infinity() are forbidden repo-wide, because the macOS
/// leg may assume finiteness and fold such a test away. "Non-finite" is exactly
/// "all eight exponent bits set" - an INTEGER test no flag can reshape.
[[nodiscard]] bool isNonFiniteBits(float v) noexcept;

// -----------------------------------------------------------------------------
// T017's tap-render helper
// -----------------------------------------------------------------------------
// Materialises a whole render's PRE-OUTPUT-STAGE tap. It is deliberately not the
// streaming renderTapBlocks() defined further down this TU (that one is declared
// after this point and cannot be called from here, and its sink shape does not
// suit the difference-of-two-whole-renders arithmetic below). The three
// per-block REQUIREs are the isolated-return definition's own preconditions: a
// short or truncated tap would silently make every measurement cover less audio
// than it claims to (plan D-8 clause 3).
struct TapRender {
    std::vector<float> left, right;
    /// The TRUE PLUGIN OUTPUT's worst |sample| over the whole render. Carried
    /// beside the tap because SC-003(b)'s precondition is a statement about the
    /// output (that it stayed clear of the limiter) while its measurement is a
    /// statement about the tap.
    float outputPeak = 0.0f;
};

template <typename Script>
[[nodiscard]] TapRender captureTapRender(Fixture& fx, std::size_t numBlocks,
                                         std::size_t blockSamples, const Script& script) {
    TapRender out;
    out.left.reserve(numBlocks * blockSamples);
    out.right.reserve(numBlocks * blockSamples);

    for (std::size_t b = 0; b < numBlocks; ++b) {
        script(b, fx);
        REQUIRE(fx.processBlock(static_cast<Steinberg::int32>(blockSamples))
                == Steinberg::kResultOk);

        const std::span<const float> tapL = fx.proc->preOutputTapLForTest();
        const std::span<const float> tapR = fx.proc->preOutputTapRForTest();
        REQUIRE(tapL.size() == blockSamples);
        REQUIRE(tapR.size() == blockSamples);
        REQUIRE_FALSE(fx.proc->preOutputTapTruncatedForTest());

        out.left.insert(out.left.end(), tapL.begin(), tapL.end());
        out.right.insert(out.right.end(), tapR.begin(), tapR.end());

        const float* outL = fx.audioL();
        const float* outR = fx.audioR();
        for (std::size_t i = 0; i < blockSamples; ++i) {
            out.outputPeak = std::max({out.outputPeak, std::fabs(outL[i]), std::fabs(outR[i])});
        }
    }
    REQUIRE(fx.checkCanaries());
    return out;
}

/// RMS over BOTH channels of a sample window, in the difference domain.
[[nodiscard]] double diffRms(const TapRender& a, const TapRender& b, std::size_t firstSample,
                             std::size_t numSamples) {
    const std::size_t end = std::min({a.left.size(), b.left.size(), firstSample + numSamples});
    if (firstSample >= end) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = firstSample; i < end; ++i) {
        const double dl = static_cast<double>(a.left[i]) - static_cast<double>(b.left[i]);
        const double dr = static_cast<double>(a.right[i]) - static_cast<double>(b.right[i]);
        sum += dl * dl + dr * dr;
    }
    return std::sqrt(sum / static_cast<double>(2u * (end - firstSample)));
}

/// M/S SIDE energy of the isolated return: side = (L - R) * 0.5, summed over the
/// window. Measured on the DIFFERENCE - i.e. on the send's own contribution -
/// which is what makes SC-003(c) discriminate "the wander images the send" from
/// "the wander images only the dry bus".
[[nodiscard]] double diffSideEnergy(const TapRender& a, const TapRender& b,
                                    std::size_t firstSample, std::size_t numSamples) {
    const std::size_t end = std::min({a.left.size(), b.left.size(), firstSample + numSamples});
    if (firstSample >= end) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = firstSample; i < end; ++i) {
        const double dl = static_cast<double>(a.left[i]) - static_cast<double>(b.left[i]);
        const double dr = static_cast<double>(a.right[i]) - static_cast<double>(b.right[i]);
        const double side = (dl - dr) * 0.5;
        sum += side * side;
    }
    return sum;
}

/// Broadband RMS over BOTH channels of one render's window.
[[nodiscard]] double tapRms(const TapRender& r, std::size_t firstSample,
                            std::size_t numSamples) {
    const std::size_t end = std::min(r.left.size(), firstSample + numSamples);
    if (firstSample >= end) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = firstSample; i < end; ++i) {
        sum += static_cast<double>(r.left[i]) * static_cast<double>(r.left[i])
               + static_cast<double>(r.right[i]) * static_cast<double>(r.right[i]);
    }
    return std::sqrt(sum / static_cast<double>(2u * (end - firstSample)));
}

/// Worst |x[i] - x[i-1]| over BOTH channels of a sample window - SC-008's
/// statistic, applied here to FR-010a's disengage edge.
[[nodiscard]] float maxPerSampleDelta(const TapRender& r, std::size_t firstSample,
                                      std::size_t numSamples) {
    const std::size_t end = std::min(r.left.size(), firstSample + numSamples);
    float worst = 0.0f;
    for (std::size_t i = std::max<std::size_t>(firstSample, 1u); i < end; ++i) {
        worst = std::max(worst, std::fabs(r.left[i] - r.left[i - 1]));
        worst = std::max(worst, std::fabs(r.right[i] - r.right[i - 1]));
    }
    return worst;
}

[[nodiscard]] double toDb(double ratio) {
    return 20.0 * std::log10(ratio);
}

/// The SC-003(b)/(c) script: the shared operating point, everything at block 0
/// so it is in force before a single sample is rendered.
void applyOrderScript(std::size_t b, Fixture& fx, double mixNormalized, double widthNormalized,
                      double gainNormalized) {
    if (b != 0) {
        return;
    }
    fx.setParam(Seraphis::kMasterGainId, gainNormalized);
    fx.setParam(Seraphis::kSoftLimitId, 0.0);  // (b)'s redundant guard: no saturation
    fx.setParam(Seraphis::kFxDelayMixId, mixNormalized);
    fx.setParam(Seraphis::kFxDelayTimeId, kOrderDelayNormalized);
    fx.setParam(Seraphis::kFxDelayFeedbackId, kOrderFeedbackNormalized);
    fx.setParam(Seraphis::kFxWidthId, widthNormalized);
    for (const Steinberg::int16 pitch : kEightNoteChord) {
        fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
    }
}

/// One whole SC-003(b)/(c) render at a given mix, width and master gain.
[[nodiscard]] TapRender renderOrderArm(double mixNormalized, double widthNormalized,
                                       double gainNormalized) {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    TapRender out = captureTapRender(
        fx, kOrderRenderBlocks, kBlockSamples,
        [mixNormalized, widthNormalized, gainNormalized](std::size_t b, Fixture& f) {
            applyOrderScript(b, f, mixNormalized, widthNormalized, gainNormalized);
        });
    // Non-vacuity for the mix-1 arm, and FR-007's prohibition for the mix-0 one.
    if (mixNormalized > 0.0) {
        REQUIRE(fx.proc->sendChunkCountForTest() > std::size_t{0});
    } else {
        REQUIRE(fx.proc->sendChunkCountForTest() == std::size_t{0});
    }
    return out;
}

/// SC-003(a)'s arm. `observeFromBlock == 0` means DETECT: start measuring the
/// output once the PRE-LIMITER tap peak first exceeds the ceiling, i.e. once the
/// limiter is provably in gain reduction. A non-zero value means "observe from
/// exactly here", which is how the positive control is held to the same window
/// as the shipped-order render without re-detecting on a tap that no longer sees
/// step 5 at all (with capability 2 the wander runs AFTER the tap is taken).
struct CeilingArm {
    bool observed = false;
    std::size_t observeFromBlock = 0;
    float worstTapPeak = 0.0f;
    float worstOutputPeak = 0.0f;
};

[[nodiscard]] CeilingArm runCeilingArm(bool stepFiveAfterOutput, std::size_t observeFromBlock) {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    const ScopedEffectsProbe guard(*fx.proc, /*bypassed*/ false,
                                   /*afterOutput*/ stepFiveAfterOutput, /*rampSnap*/ false);

    CeilingArm arm;
    std::size_t observedBlocks = 0;
    for (std::size_t b = 0; b < kLoudMaxBlocks && observedBlocks < kCeilingObserveBlocks; ++b) {
        if (b == 0) {
            fx.setParam(Seraphis::kMasterGainId, 1.0);  // -> linear 2.0
            fx.setParam(Seraphis::kPolyphonyId, 1.0);   // -> 16 voices
            fx.setParam(Seraphis::kFxDelayMixId, 1.0);
            fx.setParam(Seraphis::kFxDelayTimeId, kOrderDelayNormalized);
            fx.setParam(Seraphis::kFxDelayFeedbackId, kOrderFeedbackNormalized);
            fx.setParam(Seraphis::kFxWidthId, kOrderWidthMaxNormalized);
            // Azimuth live as well: at full deflection the pan pair reaches
            // +3.01 dB on one channel (plan D-4), so a stage that ran AFTER the
            // limiter has a second, independent way to break the ceiling - which
            // is what makes the positive control's failure certain rather than
            // dependent on how decorrelated the reverb happens to be.
            fx.setParam(Seraphis::kFxAzimuthDepthId, 1.0);
            for (const Steinberg::int16 pitch : kSixteenNoteChord) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, 1.0f, 0);
            }
        }
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        const std::span<const float> tapL = fx.proc->preOutputTapLForTest();
        const std::span<const float> tapR = fx.proc->preOutputTapRForTest();
        REQUIRE(tapL.size() == kBlockSamples);
        REQUIRE_FALSE(fx.proc->preOutputTapTruncatedForTest());

        float tapPeak = 0.0f;
        for (std::size_t i = 0; i < kBlockSamples; ++i) {
            tapPeak = std::max({tapPeak, std::fabs(tapL[i]), std::fabs(tapR[i])});
        }
        arm.worstTapPeak = std::max(arm.worstTapPeak, tapPeak);

        const bool inWindow = (observeFromBlock == 0) ? (tapPeak > kLimiterCeilingLin)
                                                      : (b >= observeFromBlock);
        if (!arm.observed && inWindow) {
            arm.observed = true;
            arm.observeFromBlock = b;
        }
        if (arm.observed) {
            const float* outL = fx.audioL();
            const float* outR = fx.audioR();
            for (std::size_t i = 0; i < kBlockSamples; ++i) {
                arm.worstOutputPeak =
                    std::max({arm.worstOutputPeak, std::fabs(outL[i]), std::fabs(outR[i])});
            }
            ++observedBlocks;
        }
    }
    REQUIRE(fx.checkCanaries());
    return arm;
}

}  // namespace

TEST_CASE("Effects chain order matches C-1", "[seraphis][effects]") {
    SECTION("ID 1400 is the only writer on output saturation") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        REQUIRE(fx.proc->engineForTest() != nullptr);

        // Precondition, not decoration: pushEffectsParams() is called ONLY from
        // process() (processor.cpp:743), so nothing has been counted yet. If
        // this ever fires, a prepare-time path started counting and clause 4's
        // "advances exactly once" would be measuring the wrong thing.
        REQUIRE(fx.proc->engSoftLimitPushCountForTest() == std::size_t{0});

        // --- 1. ID 1400 = 0.8 reaches the setter -----------------------------
        fx.setParam(Seraphis::kFxSaturationId, kSaturationNormalized);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(fx.proc->engineForTest()->getOutputSaturation()
              == Catch::Approx(kSaturationPlain).margin(1.0e-6));

        // --- 2. ID 2 OFF closes the gate -------------------------------------
        // The gate keeps its shipped Phase 8 meaning: off means NO output
        // saturation at all, whatever amount ID 1400 holds.
        fx.setParam(Seraphis::kSoftLimitId, 0.0);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(fx.proc->engineForTest()->getOutputSaturation()
              == Catch::Approx(0.0f).margin(1.0e-6));

        // --- 3. ID 2 ON again restores ID 1400's AMOUNT (the D-2 defect) -----
        // Under the shipped two-writer code this reverts to kOutputSaturation
        // (0.15f) and stays there until ID 1400 next moves. The second CHECK
        // names that failure explicitly so a red run reads as the regression it
        // is rather than as a generic tolerance miss.
        fx.setParam(Seraphis::kSoftLimitId, 1.0);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        const float restored = fx.proc->engineForTest()->getOutputSaturation();
        CHECK(restored == Catch::Approx(kSaturationPlain).margin(1.0e-6));
        CHECK(restored != Catch::Approx(Seraphis::kFxSaturationDefault).margin(1.0e-6));

        // --- 4. C-6 defaults: unchanged value, unchanged cadence -------------
        // A FRESH processor, because clauses 1-3 have moved both IDs. At the
        // defaults the composed amount is `true ? 0.15f : 0.0f`, i.e. exactly
        // what Phase 9 pushed, pushed exactly once - the cadence
        // engSoftLimitPushCountForTest() (processor.h:236) is asserted against
        // by the Phase 9 SC-007 arm, which must not move.
        //
        // The prepare-time writer seeds the VALUE but deliberately leaves
        // lastPushedSaturationValid_ false (processor.cpp:577-578), so the first
        // process() pushes once and counts once; the nine blocks after it change
        // nothing and must not count at all.
        Fixture defaults;
        REQUIRE(defaults.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        REQUIRE(defaults.proc->engineForTest() != nullptr);
        REQUIRE(defaults.proc->engSoftLimitPushCountForTest() == std::size_t{0});

        defaults.renderBlocks(std::size_t{10}, kBlockSamples);

        CHECK(defaults.proc->engineForTest()->getOutputSaturation()
              == Catch::Approx(Seraphis::kFxSaturationDefault).margin(1.0e-6));
        CHECK(defaults.proc->engSoftLimitPushCountForTest() == std::size_t{1});
    }

    // =========================================================================
    // FR-041 clause 6 + plan D-8 clause 3 - THE TAP, AND ITS TRUNCATION FLAG
    // =========================================================================
    // Every isolated-return criterion in this phase (SC-003(b) onward, SC-005,
    // SC-007, SC-011a, SC-019) is DEFINED to measure at this tap, so that the
    // output stage - TapeSaturator + TruePeakLimiter, both nonlinear in the
    // quantity being measured - is out of the measured path. Two properties have
    // to hold before any of them means anything:
    //
    //   (i)  the tap really is the bus as the output stage will see it, i.e. it
    //        is taken BEFORE processOutputStage and not after it (clause 3), and
    //   (ii) it says so when it could not hold the whole block (clause 2).
    //        Without (ii) a 4096-sample host block silently yields a half-length
    //        tap and every criterion above measures HALF A RENDER with no error
    //        signal - the defect plan D-8 clause 3 found.
    // =========================================================================
    SECTION("the pre-output tap is honest about truncation") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        // --- 1. A block the tap holds whole ----------------------------------
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(fx.proc->preOutputTapLForTest().size() == kBlockSamples);
        CHECK(fx.proc->preOutputTapRForTest().size() == kBlockSamples);
        CHECK_FALSE(fx.proc->preOutputTapTruncatedForTest());
        // FR-041 clause 1's divisor, asserted where it is cheap to see: ONE per
        // process() CALL. renderSlice() is not the counting site - a block may
        // carry up to eight sub-slices, and a per-slice divisor would make
        // SC-013's per-block budget structurally unable to fail.
        CHECK(fx.proc->effectsStageProcessCallsForTest() == std::size_t{1});

        // --- 2. A block LARGER than the tap ----------------------------------
        // 4096 samples is two 2048-sample slices and still exactly ONE process()
        // call, so the tap saturates at kMaxBlockSamples and the flag is raised.
        REQUIRE(fx.processBlock(kOversizeBlock) == Steinberg::kResultOk);
        CHECK(fx.proc->preOutputTapLForTest().size() == kMaxTapSamples);
        CHECK(fx.proc->preOutputTapRForTest().size() == kMaxTapSamples);
        CHECK(fx.proc->preOutputTapTruncatedForTest());
        CHECK(fx.proc->effectsStageProcessCallsForTest() == std::size_t{2});
        CHECK(fx.checkCanaries());

        // --- 3. All three FR-040 capabilities are declared and reachable ------
        // Compiling this block IS most of the assertion (a probe that declared
        // fewer capabilities would not build here), and running it exercises the
        // RAII restore path that keeps a set mode from leaking into the rest of
        // the suite.
        {
            const ScopedEffectsProbe guard(*fx.proc, /*bypassed*/ true,
                                           /*afterOutput*/ true, /*rampSnap*/ true);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            CHECK(fx.proc->preOutputTapLForTest().size() == kBlockSamples);
        }
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(fx.proc->preOutputTapLForTest().size() == kBlockSamples);
        CHECK(fx.checkCanaries());

        // --- 4. The tap is taken BEFORE the output stage ----------------------
        // The precondition is what makes this non-vacuous: below the ceiling the
        // limiter is exactly unity (true_peak_limiter.h:150-160) and only the
        // saturator's small blend would separate the two signals. Driven into
        // gain reduction, a tap taken on the WRONG side of processOutputStage
        // would be identical to the output and the floor below would fail.
        Fixture loud;
        REQUIRE(loud.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        loud.setParam(Seraphis::kMasterGainId, 1.0);  // -> linear 2.0
        loud.setParam(Seraphis::kPolyphonyId, 1.0);   // -> 16 voices
        for (const Steinberg::int16 pitch : kSixteenNoteChord) {
            loud.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, 1.0f, 0);
        }

        bool limiterInGainReduction = false;
        float worstTapPeak = 0.0f;
        float tapVsOutDelta = 0.0f;
        for (std::size_t b = 0; b < kLoudMaxBlocks && !limiterInGainReduction; ++b) {
            REQUIRE(loud.processBlock(kBlock) == Steinberg::kResultOk);

            const std::span<const float> tapL = loud.proc->preOutputTapLForTest();
            REQUIRE(tapL.size() == kBlockSamples);
            REQUIRE_FALSE(loud.proc->preOutputTapTruncatedForTest());

            float peak = 0.0f;
            for (const float v : tapL) {
                peak = std::max(peak, std::fabs(v));
            }
            worstTapPeak = std::max(worstTapPeak, peak);
            if (peak <= kLimiterCeilingLin) {
                continue;
            }

            limiterInGainReduction = true;
            const float* out = loud.audioL();
            for (std::size_t i = 0; i < kBlockSamples; ++i) {
                tapVsOutDelta = std::max(tapVsOutDelta, std::fabs(tapL[i] - out[i]));
            }
        }

        INFO("worst pre-output-stage tap peak = " << worstTapPeak << " (limiter ceiling "
                                                  << kLimiterCeilingLin << ")");
        REQUIRE(limiterInGainReduction);
        CHECK(tapVsOutDelta > kTapVsOutputFloor);
        CHECK(loud.checkCanaries());
    }

    // =========================================================================
    // FR-004 - THE SEND CARRIES NO CURRENT-BLOCK DRY
    // =========================================================================
    // C-2 clause 1 pushes setDryWetMix(1.0f) BEFORE SpectralDelay::prepare(),
    // and the ordering is load-bearing rather than stylistic: setDryWetMix only
    // sets a smoother TARGET (spectral_delay.h:500-503), and that smoother is
    // advanced exactly ONCE PER process() CALL (:373, :389) despite carrying a
    // per-sample 50 ms coefficient (:184-194). Pushed AFTER prepare it would
    // creep from kDefaultDryWet = 0.5f (:109) toward 1.0 by ~0.04 % of the
    // remaining distance per call - i.e. for tens of seconds the send would
    // return ~50 % of the CURRENT chunk's own input, un-aligned against a wet
    // that is fftSize samples late. That is the smeared comb the whole
    // parallel-send convention exists to prevent.
    //
    // HOW THE ISOLATED RETURN IS OBTAINED, and why it is exact. The send only
    // ever ADDS to the bus, so the return is the difference between two renders
    // that differ ONLY in whether FR-040's probe skips C-1 steps 4 and 5. The
    // difference really is exact and not merely close, because
    // updateEffectsBypassState() and setParamSmootherTargets() both run in the
    // PRE-SLICE block, outside the probe's branch: the return-gain ramp is
    // therefore un-settled in BOTH renders, both subdivide into 64-sample
    // sub-slices on the same grid, and every other stage sees identical inputs.
    // Where the return is zero the bus is bit-identical and so is the plugin
    // output - which is why this clause can measure the OUTPUT and never has to
    // reason about TapeSaturator or TruePeakLimiter.
    //
    // THREE RENDERS RUN, TWO ARE MEASURED, for the reason reprepare()'s banner
    // measures: a re-prepare returns this instance to a REPRODUCIBLE state, not
    // to a VIRGIN one.
    // =========================================================================
    SECTION("the send carries no current-block dry") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        // Block 0 drives kFxDelayMixId back to 0 EXPLICITLY. The atomics survive
        // a re-prepare, so without it render B would start with the 1.0 render A
        // left behind and would engage the send at block 0 instead of block 4.
        const auto script = [&fx](std::size_t b, Krate::Test::EventList&,
                                  SeraphisTest::ParameterChanges& params) {
            if (b == 0) {
                params.addQueue(Seraphis::kFxDelayMixId).addTestPoint(0, 0.0);
                for (const Steinberg::int16 pitch : kEightNoteChord) {
                    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
                }
            } else if (b == kDryProbeEngageBlock) {
                params.addQueue(Seraphis::kFxDelayMixId).addTestPoint(0, 1.0);
            }
        };

        fx.renderBlocks(kDryProbeBlocks, kBlockSamples, script);  // warm-up, discarded
        fx.capturedL.clear();
        fx.capturedR.clear();
        REQUIRE(fx.checkCanaries());

        // --- Render A: the send skipped -------------------------------------
        reprepare(fx);
        {
            const ScopedEffectsProbe guard(*fx.proc, /*bypassed*/ true, /*afterOutput*/ false,
                                           /*rampSnap*/ false);
            fx.renderBlocks(kDryProbeBlocks, kBlockSamples, script);
        }
        const std::vector<float> noSendL = fx.capturedL;
        fx.capturedL.clear();
        fx.capturedR.clear();
        REQUIRE(noSendL.size() == kDryProbeBlocks * kBlockSamples);
        REQUIRE(fx.checkCanaries());

        // --- Render B: the same instance, re-prepared, the send RUNNING ------
        reprepare(fx);
        fx.renderBlocks(kDryProbeBlocks, kBlockSamples, script);
        REQUIRE(fx.capturedL.size() == noSendL.size());
        REQUIRE(fx.checkCanaries());
        // The send really did run - otherwise every clause below is vacuous.
        REQUIRE(fx.proc->sendChunkCountForTest() > std::size_t{0});

        // --- Non-vacuity: the bus is LOUD across the measured window ---------
        float busPeak = 0.0f;
        for (std::size_t i = kDryProbeEngageSample;
             i < kDryProbeEngageSample + kDryProbeSilentSamples; ++i) {
            busPeak = std::max(busPeak, std::fabs(fx.capturedL[i]));
        }
        INFO("bus peak across the FR-004 window = " << busPeak);
        REQUIRE(busPeak > kDryProbeBusFloor);

        // --- THE CLAUSE: no return at all for fftSize + one chunk ------------
        float worstEarlyReturn = 0.0f;
        for (std::size_t i = kDryProbeEngageSample;
             i < kDryProbeEngageSample + kDryProbeSilentSamples; ++i) {
            worstEarlyReturn = std::max(worstEarlyReturn, std::fabs(fx.capturedL[i] - noSendL[i]));
        }
        INFO("worst isolated return inside the first "
             << kDryProbeSilentSamples << " samples = " << worstEarlyReturn);
        CHECK(worstEarlyReturn < kDryProbeSilentFloor);

        // --- And the return DOES arrive afterwards ---------------------------
        // Without this a send that produced nothing at all would satisfy the
        // clause above perfectly.
        float worstLateReturn = 0.0f;
        for (std::size_t i = kDryProbeEngageSample + kDryProbeSilentSamples; i < noSendL.size();
             ++i) {
            worstLateReturn = std::max(worstLateReturn, std::fabs(fx.capturedL[i] - noSendL[i]));
        }
        INFO("worst isolated return after the window = " << worstLateReturn);
        CHECK(worstLateReturn > kDryProbeReturnFloor);
    }

    // =========================================================================
    // FR-009a - THE DRAIN'S ENERGY FLOOR ENDS THE WINDOW EARLY
    // =========================================================================
    // The drain is fed SILENCE (plan 3.1's push step), so a bypass excursion's
    // worst-case cost is bounded BY ENERGY rather than by wall clock: the send's
    // own per-bin feedback decays the tail and kFxSendDrainFloor = 1e-6 ends the
    // window the moment the chunk the send produced falls under it.
    //
    // The operating point is chosen so the FLOOR provably fires before the CAP,
    // which is the only way the floor is observable at all - see the banner on
    // kFloorFeedbackPlain. The case additionally ASSERTS that the cap could not
    // have fired, so a "frozen count" cannot be credited to the wrong exit.
    // =========================================================================
    SECTION("the drain's energy floor ends the window early") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        // The chunk count must be exactly 0 while the send has never been
        // engaged - FR-007's prohibition, and the baseline for everything below.
        REQUIRE(fx.proc->sendChunkCountForTest() == std::size_t{0});

        // The automation MUST be placed from inside the script: renderBlocks()
        // clears the event list and the parameter queues before EVERY block,
        // block 0 included, so anything queued beforehand is discarded.
        const auto engageScript = [&fx](std::size_t b, Krate::Test::EventList&,
                                        SeraphisTest::ParameterChanges& params) {
            if (b != 0) {
                return;
            }
            params.addQueue(Seraphis::kFxDelayFeedbackId).addTestPoint(0, kFloorFeedbackNormalized);
            params.addQueue(Seraphis::kFxDelayTimeId).addTestPoint(0, kFloorDelayNormalized);
            params.addQueue(Seraphis::kFxDelayMixId).addTestPoint(0, kInvMixNormalized);
            for (const Steinberg::int16 pitch : kEightNoteChord) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
            }
        };
        fx.renderBlocks(kFloorBuildBlocks, kBlockSamples, engageScript);

        const std::size_t chunksAtBypass = fx.proc->sendChunkCountForTest();
        INFO("chunks produced while ACTIVE = " << chunksAtBypass);
        REQUIRE(chunksAtBypass > std::size_t{0});

        // --- Bypass: the drain starts ----------------------------------------
        // The notes stay held, so the bus is still loud. That is deliberate: the
        // count freezing anyway is what proves the drain is fed SILENCE rather
        // than the live bus.
        const auto bypassScript = [](std::size_t b, Krate::Test::EventList&,
                                     SeraphisTest::ParameterChanges& params) {
            if (b == 0) {
                params.addQueue(Seraphis::kFxDelayMixId).addTestPoint(0, 0.0);
            }
        };
        fx.renderBlocks(kFloorObserveBlocks, kBlockSamples, bypassScript);

        const std::size_t chunksAfterFloor = fx.proc->sendChunkCountForTest();
        INFO("chunks produced after ~0.78 s of drain = " << chunksAfterFloor);
        // The drain really ran: a send that jumped straight to Bypassed would
        // annihilate the tail, which is exactly what FR-008/FR-009a exist to
        // prevent (and what a missing fxDrainPeak_ re-arm would produce).
        CHECK(chunksAfterFloor > chunksAtBypass);

        // --- The count is FROZEN well before the 2 s cap could fire ----------
        fx.renderBlocks(kFloorFreezeBlocks, kBlockSamples);

        const std::size_t drainSamples = (kFloorObserveBlocks + kFloorFreezeBlocks) * kBlockSamples;
        const auto capSamples =
            static_cast<std::size_t>(Seraphis::kFxSendDrainMs * 0.001f * kSampleRate);
        INFO("drain rendered = " << drainSamples << " samples, cap = " << capSamples);
        // If this ever fails the case has stopped measuring the floor: the
        // wall-clock cap would end the drain on its own.
        REQUIRE(drainSamples < capSamples);

        CHECK(fx.proc->sendChunkCountForTest() == chunksAfterFloor);
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // SC-003(a) - C-1 STEP 5 PRECEDES STEP 6 (the limiter is ALWAYS LAST)
    // =========================================================================
    // THIS CLAUSE ALONE MEASURES THE TRUE PLUGIN OUTPUT, and the carve-out is
    // deliberate (spec Clarifications Q4, amended): the limiter IS its subject,
    // so reading at the pre-output tap would make it vacuous. The tap is used
    // only to establish the PRECONDITION - that the pre-limiter peak exceeds
    // kLimiterCeilingLin, i.e. the limiter is provably in gain reduction.
    // Without that precondition the clause would pass on any render quiet enough
    // that the limiter is exactly unity (true_peak_limiter.h:150-160) and the
    // wander's position in the chain would be untested.
    //
    // WHY THE POSITIVE CONTROL IS MANDATORY. processor.cpp's master-gain banner
    // records that a post-limiter multiply is FORBIDDEN because it makes the
    // ceiling bound "unsatisfiable by construction". With FR-040 capability 2
    // running C-1 step 5 AFTER step 6, that is exactly the chain this build
    // renders - width 200 % reconstructs L as 1.5*L - 0.5*R (up to 2x on a
    // decorrelated bus) and the azimuth pair reaches +3.01 dB per channel at full
    // deflection (plan D-4) - so the same render MUST break the ceiling. If it
    // did not, the shipped order's pass would be evidence of nothing.
    // =========================================================================
    SECTION("step 5 precedes step 6 - the limiter is still last") {
        const float allowedPeak =
            kLimiterCeilingLin * std::pow(10.0f, kCeilingAllowanceDb / 20.0f);
        REQUIRE(allowedPeak > kLimiterCeilingLin);

        // --- The shipped order ------------------------------------------------
        const CeilingArm shipped = runCeilingArm(/*stepFiveAfterOutput*/ false,
                                                 /*observeFromBlock*/ 0);
        INFO("worst pre-limiter tap peak = " << shipped.worstTapPeak << " (ceiling "
                                             << kLimiterCeilingLin << "), gain reduction from block "
                                             << shipped.observeFromBlock);
        // THE PRECONDITION. If this fires the clause never measured a limiting
        // render and every assertion below is vacuous.
        REQUIRE(shipped.observed);
        REQUIRE(shipped.observeFromBlock > std::size_t{0});
        REQUIRE(shipped.worstTapPeak > kLimiterCeilingLin);

        INFO("shipped-order output peak = " << shipped.worstOutputPeak << " (allowed "
                                            << allowedPeak << ")");
        CHECK(shipped.worstOutputPeak <= allowedPeak);
        CHECK_FALSE(isNonFiniteBits(shipped.worstOutputPeak));

        // --- POSITIVE CONTROL: step 5 AFTER step 6 must FAIL the same bound ---
        // Held to the SAME observation window as the shipped arm rather than
        // re-detecting: with capability 2 the tap is taken before the wander, so
        // its peak is a different quantity and detecting on it would compare two
        // different stretches of the render. The level trajectory itself is
        // identical in both arms - runSendStage() runs on the same pre-wander,
        // post-master-gain bus either way.
        const CeilingArm control = runCeilingArm(/*stepFiveAfterOutput*/ true,
                                                 shipped.observeFromBlock);
        REQUIRE(control.observed);
        INFO("step-5-after-step-6 output peak = " << control.worstOutputPeak << " (allowed "
                                                  << allowedPeak << ")");
        CHECK(control.worstOutputPeak > allowedPeak);
    }

    // =========================================================================
    // SC-003(b) - C-1 STEP 4 FOLLOWS STEP 3 (the send is POST-master-gain)
    // =========================================================================
    // Measured AT THE TAP, so TapeSaturator and TruePeakLimiter are out of the
    // measured quantity entirely and the argument is about a linear chain.
    //
    // If the send were tapped BEFORE the master gain, its return would be
    // gain-independent and doubling the fader would leave the isolated return's
    // RMS unmoved (0 dB, not 6.02) - or, if the return were then also summed
    // ahead of the fader, it would scale but the master gain would no longer be
    // the single uniform scalar on everything the limiter sees, which is the
    // property Phase 9's ceiling argument rests on.
    // =========================================================================
    SECTION("step 4 follows step 3 - the send tracks the master gain") {
        const float headroomPeak =
            kLimiterCeilingLin * std::pow(10.0f, kOrderHeadroomDb / 20.0f);

        const TapRender lowDry =
            renderOrderArm(0.0, kOrderWidthMaxNormalized, kOrderGainLowNormalized);
        const TapRender lowWet =
            renderOrderArm(1.0, kOrderWidthMaxNormalized, kOrderGainLowNormalized);
        const TapRender highDry =
            renderOrderArm(0.0, kOrderWidthMaxNormalized, kOrderGainHighNormalized);
        const TapRender highWet =
            renderOrderArm(1.0, kOrderWidthMaxNormalized, kOrderGainHighNormalized);

        // THE REDUNDANT GUARD (tasks.md T017): kSoftLimitId is off in the script
        // and the level is chosen so the OUTPUT stays clear of the limiter, so
        // even a reader who distrusts the tap can see the render never entered a
        // nonlinearity.
        INFO("output peaks: low=" << lowWet.outputPeak << " high=" << highWet.outputPeak
                                  << " (must stay <= " << headroomPeak << ")");
        CHECK(lowWet.outputPeak <= headroomPeak);
        CHECK(highWet.outputPeak <= headroomPeak);

        constexpr std::size_t kFirst = kOrderMeasureFirstBlock * kBlockSamples;
        constexpr std::size_t kSpan = kOrderMeasureBlocks * kBlockSamples;

        const double lowReturn = diffRms(lowWet, lowDry, kFirst, kSpan);
        const double highReturn = diffRms(highWet, highDry, kFirst, kSpan);

        // Non-vacuity: two silences would scale perfectly.
        INFO("isolated-return RMS: low=" << lowReturn << " high=" << highReturn);
        REQUIRE(lowReturn > kOrderReturnRmsFloor);
        REQUIRE(highReturn > kOrderReturnRmsFloor);

        const double deltaDb = toDb(highReturn / lowReturn);
        INFO("isolated-return delta = " << deltaDb << " dB (expected "
                                        << kOrderExpectedGainDeltaDb << " +/- "
                                        << kOrderGainToleranceDb << ")");
        CHECK(std::fabs(deltaDb - kOrderExpectedGainDeltaDb) < kOrderGainToleranceDb);
    }

    // =========================================================================
    // SC-003(c) - C-1 STEP 4 PRECEDES STEP 5 (the wander images the SEND too)
    // =========================================================================
    // Measured on the ISOLATED RETURN, never on the bus, and that is the whole
    // discriminator: a build that applied the width to the dry bus only - i.e.
    // that ran step 5 BEFORE step 4 and summed the return afterwards - would move
    // the bus's side energy with kFxWidthId exactly as expected while the send's
    // returns sat statically imaged outside the field. The difference of the two
    // renders sees only the send, so that build fails here.
    //
    // MidSideProcessor is linear, so diff(MS(bus + send), MS(bus)) = MS(send) and
    // the ideal is exact: side scales with width/100, side ENERGY with its
    // square, and 0 % carries no side at all (side is multiplied by 0.0f, so L
    // and R are reconstructed from mid alone). The 0 % clause is asserted as a
    // relative bound rather than `== 0.0` for the libm reason recorded on
    // kOrderMonoSideRelativeBound - not because a leak would be tolerated.
    // =========================================================================
    SECTION("step 4 precedes step 5 - the send is inside the stereo field") {
        constexpr std::size_t kFirst = kOrderMeasureFirstBlock * kBlockSamples;
        constexpr std::size_t kSpan = kOrderMeasureBlocks * kBlockSamples;

        const auto sideEnergyAtWidth = [](double widthNormalized) {
            const TapRender dry = renderOrderArm(0.0, widthNormalized, kOrderGainLowNormalized);
            const TapRender wet = renderOrderArm(1.0, widthNormalized, kOrderGainLowNormalized);
            return diffSideEnergy(wet, dry, kFirst, kSpan);
        };

        const double sideMono = sideEnergyAtWidth(kOrderWidthMonoNormalized);   // 0 %
        const double sideUnity = sideEnergyAtWidth(kOrderWidthUnityNormalized);  // 100 %
        const double sideWide = sideEnergyAtWidth(kOrderWidthMaxNormalized);     // 200 %

        INFO("isolated-return side energy: 0%=" << sideMono << " 100%=" << sideUnity
                                                << " 200%=" << sideWide);

        // Non-vacuity at unity, where the FR-010 skip is in force and the return
        // is imaged exactly as the send produced it.
        REQUIRE(sideUnity > kOrderSideEnergyFloor);

        // MONOTONIC across the registered range.
        CHECK(sideMono < sideUnity);
        CHECK(sideUnity < sideWide);

        // At 0 % the isolated return carries NO side at all (see
        // kOrderMonoSideRelativeBound for why this is relative and not `== 0.0`).
        CHECK(sideMono < sideUnity * kOrderMonoSideRelativeBound);

        // And 200 % carries 4x the side ENERGY of 100 % - i.e. 2x the side
        // amplitude, 6.02 dB - the ideal factor, within 0.5 dB.
        const double wideDeltaDb = toDb(std::sqrt(sideWide / sideUnity));
        INFO("200% vs 100% side delta = " << wideDeltaDb << " dB (expected "
                                          << kOrderExpectedSideDeltaDb << " +/- "
                                          << kOrderSideToleranceDb << ")");
        CHECK(std::fabs(wideDeltaDb - kOrderExpectedSideDeltaDb) < kOrderSideToleranceDb);
    }

    // =========================================================================
    // FR-010 / plan D-4 - THE AZIMUTH PAIR IS UNITY AT CENTRE
    // =========================================================================
    // equalPowerGains is a CROSSFADE law (crossfade_utils.h:50-53): it preserves
    // energy when its two gains are applied to two DIFFERENT signals that are
    // then SUMMED. Applied to the two channels of ONE stereo bus the quantity
    // that must stay constant is gL^2 + gR^2, so the raw law's cos(pi/4) = 0.7071
    // on BOTH channels drops the whole bus -3.01 dB the instant kFxAzimuthDepthId
    // leaves 0 - and puts it back when FR-010's skip re-engages.
    //
    // That is a STEADY-STATE LEVEL STEP AS A FUNCTION OF A DEPTH CONTROL, which
    // no smoother removes because it is not a transient, and which SC-008's
    // maxPerSampleDelta would therefore never flag (FR-010a spreads it over
    // ~960 samples). Stepping the depth from 0 to a small epsilon crosses the
    // skip boundary while barely moving the pan position, so an uncompensated
    // build fails this by the full 3.01 dB. THIS is what makes
    // kFxAzimuthCentreComp a measured constant rather than an asserted one.
    // =========================================================================
    SECTION("azimuth is unity at centre") {
        const auto renderAtAzimuthDepth = [](double azimuthNormalized) {
            Fixture fx;
            REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
            TapRender out = captureTapRender(
                fx, kAzimuthRenderBlocks, kBlockSamples,
                [azimuthNormalized](std::size_t b, Fixture& f) {
                    if (b != 0) {
                        return;
                    }
                    // Width and wander depth stay at their C-6 defaults, so the
                    // ONLY thing that crosses FR-010's predicate is the azimuth
                    // depth - which is what makes the comparison a measurement
                    // of the pan pair and of nothing else.
                    f.setParam(Seraphis::kFxAzimuthDepthId, azimuthNormalized);
                    for (const Steinberg::int16 pitch : kEightNoteChord) {
                        f.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
                    }
                });
            return out;
        };

        const TapRender skipped = renderAtAzimuthDepth(0.0);
        const TapRender engaged = renderAtAzimuthDepth(kAzimuthEpsilonNormalized);

        constexpr std::size_t kFirst = kAzimuthMeasureFirstBlock * kBlockSamples;
        constexpr std::size_t kSpan =
            (kAzimuthRenderBlocks - kAzimuthMeasureFirstBlock) * kBlockSamples;

        const double rmsSkipped = tapRms(skipped, kFirst, kSpan);
        const double rmsEngaged = tapRms(engaged, kFirst, kSpan);

        INFO("broadband RMS: skipped=" << rmsSkipped << " engaged=" << rmsEngaged);
        REQUIRE(rmsSkipped > kOrderReturnRmsFloor);
        REQUIRE(rmsEngaged > kOrderReturnRmsFloor);

        const double deltaDb = toDb(rmsEngaged / rmsSkipped);
        INFO("crossing FR-010's skip boundary moved the bus " << deltaDb
             << " dB (uncompensated: -3.01 dB; allowed: +/-" << kAzimuthUnityToleranceDb << ")");
        CHECK(std::fabs(deltaDb) < kAzimuthUnityToleranceDb);
    }

    // =========================================================================
    // FR-024 - THE AZIMUTH PAN PAIR IS EVALUATED ON THE 64-SAMPLE GRID
    // =========================================================================
    // Plan R-14's defect is INVISIBLE IN THE AUDIO, and that is why this section
    // reads a witness instead of a render. Both drifts are advanced ONCE per
    // process() call (FR-011) and getCurrentValue() is a pure read
    // (brownian_drift.h:212), while the two depth smoothers are advanced by
    // advanceParamSmoothers() outside renderSlice() - so inside ONE slice every
    // control chunk computes the SAME target and a control loop that ran to
    // completion before a single globalMs_.process(l, r, l, r, n) call would
    // sound identical while delivering a per-SLICE grid of up to 2048 samples
    // (~43 ms) instead of 64.
    //
    // The witness records one entry per audio sub-chunk, written AFTER that
    // sub-chunk's samples were processed and carrying the sub-chunk's own length.
    // The non-interleaved shape therefore reports ONE entry of length 2048; a
    // per-sample cos/sin reports 2048 entries of length 1; the shipped shape
    // reports exactly 32 entries of length 64.
    // =========================================================================
    SECTION("the azimuth pan pair is evaluated on the 64-sample grid") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kGridBlock) == Steinberg::kResultOk);

        for (std::size_t b = 0; b <= kGridSettleBlocks; ++b) {
            if (b == 0) {
                fx.setParam(Seraphis::kFxWidthId, kOrderWidthMaxNormalized);
                fx.setParam(Seraphis::kFxWanderDepthId, 1.0);
                fx.setParam(Seraphis::kFxAzimuthDepthId, 1.0);
                for (const Steinberg::int16 pitch : kEightNoteChord) {
                    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
                }
            }
            REQUIRE(fx.processBlock(kGridBlock) == Steinberg::kResultOk);
        }

        // The block is exactly kMaxBlockSamples, so it is ONE slice and the tap
        // holds all of it (plan D-8 clause 3's rule for tap-measuring cases).
        REQUIRE(fx.proc->preOutputTapLForTest().size() == kGridBlockSamples);
        REQUIRE_FALSE(fx.proc->preOutputTapTruncatedForTest());
        // Non-vacuity: a skipped stage evaluates nothing at all.
        REQUIRE(Probe::wanderRunsEffective(*fx.proc));

        const std::size_t updates = Probe::wanderControlUpdates(*fx.proc);
        INFO("control evaluations over one " << kGridBlockSamples
                                             << "-sample block = " << updates);
        // AT MOST 32 - the FR-024 bound, i.e. no per-sample transcendentals.
        CHECK(updates <= kGridExpectedUpdates);
        // And EXACTLY 32, which is the interleaving: one control evaluation per
        // 64 samples of audio, not one per slice.
        CHECK(updates == kGridExpectedUpdates);

        const std::span<const std::uint16_t> lengths = Probe::wanderChunkLengths(*fx.proc);
        REQUIRE(lengths.size() == updates);
        std::size_t covered = 0;
        for (const std::uint16_t len : lengths) {
            CHECK(len <= kGridControlChunk);
            covered += len;
        }
        // Every sample of the block was carried by some sub-chunk: a witness that
        // logged the control loop but not the audio could not satisfy this.
        CHECK(covered == kGridBlockSamples);

        // "At most 32 DISTINCT target values" over the block. With every
        // class-(b) smoother settled the drifts and depths are constant across
        // this single slice, so the honest expectation is between 1 and 32 - the
        // bound is what excludes a per-sample evaluation.
        const std::span<const float> targetsL = Probe::azimuthTargetsL(*fx.proc);
        const std::span<const float> targetsR = Probe::azimuthTargetsR(*fx.proc);
        REQUIRE(targetsL.size() == updates);
        REQUIRE(targetsR.size() == updates);

        std::vector<float> distinct(targetsL.begin(), targetsL.end());
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        INFO("distinct azimuth targets = " << distinct.size());
        CHECK(distinct.size() >= std::size_t{1});
        CHECK(distinct.size() <= kGridExpectedUpdates);

        for (const float t : targetsL) {
            CHECK_FALSE(isNonFiniteBits(t));
        }
        for (const float t : targetsR) {
            CHECK_FALSE(isNonFiniteBits(t));
        }
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // FR-024a - DEPTH NEVER REACHES BrownianDrift::setDepth()
    // =========================================================================
    // Both depth IDs are a PLUGIN-SIDE MULTIPLY of getCurrentValue(), and the
    // reason is FR-010: the bypass predicate needs the depth as a plain scalar it
    // can compare against 0.0f on the block the host wrote it. Pushed through
    // BrownianDrift::setDepth() (brownian_drift.h:159) the value would sit behind
    // that component's own kDriftOutputSmoothMs = 150 ms output smoother (:103),
    // so a depth of exactly 0 would still be emitting a decaying non-zero for
    // ~150 ms and the RAW predicate could not be answered at all.
    //
    // ON "THE SAME BLOCK", stated precisely so the two halves of the design are
    // not confused. What is immediate is the RAW predicate (FR-010): it flips on
    // the very block the host writes 0. What is deliberately NOT immediate is the
    // SKIP itself (FR-010a): the disengage latch keeps the stage running until
    // every wander smoother has reached identity, because skipping applies exact
    // identity and an unlatched skip would step the image in one sample. Both are
    // measured below - the predicate on its block, the skip's EXACTNESS as
    // bit-identity against a probe-bypassed render once the stated settle window
    // has elapsed.
    // =========================================================================
    SECTION("depth never reaches BrownianDrift::setDepth") {
        // --- 1. setMean(0.0f) is PUSHED, and depth is left at its prepared value
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        CHECK(Probe::widthDrift(*fx.proc).getMean() == 0.0f);    // brownian_drift.h:171
        CHECK(Probe::azimuthDrift(*fx.proc).getMean() == 0.0f);
        CHECK(Probe::widthDrift(*fx.proc).getDepth()
              == Krate::DSP::BrownianDrift::kDefaultDepth);      // :170
        CHECK(Probe::azimuthDrift(*fx.proc).getDepth()
              == Krate::DSP::BrownianDrift::kDefaultDepth);

        // FR-010a's window is a STATED sample count, not a guess: three time
        // constants of the slower of MidSideProcessor::kDefaultSmoothingMs
        // (10 ms) and kParamSmoothMs (20 ms), at 48 kHz.
        CHECK(Probe::wanderSettleSamples(*fx.proc) == kExpectedWanderSettleSamples);

        // --- 2. A full 0 -> 1 -> 0 sweep of BOTH depth IDs --------------------
        fx.setParam(Seraphis::kFxWanderDepthId, 1.0);
        fx.setParam(Seraphis::kFxAzimuthDepthId, 1.0);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        // The raw predicate answered on THIS block - the property the plugin-side
        // multiply exists for.
        CHECK(Probe::wanderRunsRaw(*fx.proc));
        fx.renderBlocks(kDepthLegBlocks, kBlockSamples);
        CHECK(Probe::widthDrift(*fx.proc).getDepth()
              == Krate::DSP::BrownianDrift::kDefaultDepth);
        CHECK(Probe::azimuthDrift(*fx.proc).getDepth()
              == Krate::DSP::BrownianDrift::kDefaultDepth);

        fx.setParam(Seraphis::kFxWanderDepthId, 0.0);
        fx.setParam(Seraphis::kFxAzimuthDepthId, 0.0);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        // SAME BLOCK: width is at its 100 % default and both depths are exactly
        // 0, so FR-010's predicate is false immediately. A 150 ms drift-side
        // depth could not produce this at all.
        CHECK_FALSE(Probe::wanderRunsRaw(*fx.proc));
        // ...while FR-010a's latch keeps the STAGE running, which is the other
        // half of the design and the reason the disengage does not click.
        CHECK(Probe::wanderRunsEffective(*fx.proc));

        fx.renderBlocks(kDepthLegBlocks, kBlockSamples);
        CHECK(Probe::widthDrift(*fx.proc).getDepth()
              == Krate::DSP::BrownianDrift::kDefaultDepth);
        CHECK(Probe::azimuthDrift(*fx.proc).getDepth()
              == Krate::DSP::BrownianDrift::kDefaultDepth);
        // Settled: the stage has skipped again and the M/S stage is back at unity.
        CHECK_FALSE(Probe::wanderRunsEffective(*fx.proc));
        CHECK(Probe::midSide(*fx.proc).getWidth()
              == Krate::DSP::MidSideProcessor::kDefaultWidth);
        REQUIRE(fx.checkCanaries());

        // --- 3. The skip is EXACT, measured as bit-identity -------------------
        // Two fresh instances running the IDENTICAL script - one with FR-040's
        // probe skipping C-1 steps 4 and 5 throughout. kFxDelayMixId stays at its
        // C-6 default 0, so the send never runs in either render and the only
        // thing the probe removes is the wander. Everything upstream of the
        // wander is therefore bit-identical by construction, and once the stage
        // has legitimately skipped again the two taps must agree EXACTLY - which
        // is the property SC-002 asserts globally and this clause asserts across
        // a disengage.
        // One live leg, then the disengage, then the wait window, then a further
        // leg's worth of settled render for the identity clause to measure over.
        constexpr std::size_t kSweepBlocks =
            2u * kDepthLegBlocks + 2u * kDepthIdentityWaitBlocks;
        constexpr std::size_t kDisengageBlock = kDepthLegBlocks;

        const auto sweepScript = [](std::size_t b, Fixture& f) {
            if (b == 0) {
                f.setParam(Seraphis::kFxWanderDepthId, 1.0);
                f.setParam(Seraphis::kFxAzimuthDepthId, 1.0);
                for (const Steinberg::int16 pitch : kEightNoteChord) {
                    f.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
                }
            } else if (b == kDepthLegBlocks) {
                f.setParam(Seraphis::kFxWanderDepthId, 0.0);
                f.setParam(Seraphis::kFxAzimuthDepthId, 0.0);
            }
        };

        TapRender live;
        {
            Fixture wander;
            REQUIRE(wander.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
            live = captureTapRender(wander, kSweepBlocks, kBlockSamples, sweepScript);
            REQUIRE(wander.proc->sendChunkCountForTest() == std::size_t{0});
        }

        TapRender bypassed;
        {
            Fixture skipped;
            REQUIRE(skipped.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
            const ScopedEffectsProbe guard(*skipped.proc, /*bypassed*/ true,
                                           /*afterOutput*/ false, /*rampSnap*/ false);
            bypassed = captureTapRender(skipped, kSweepBlocks, kBlockSamples, sweepScript);
        }

        REQUIRE(live.left.size() == bypassed.left.size());

        // Non-vacuity: while the wander was LIVE the two renders must differ, or
        // the identity below would be trivially true for a stage that never ran.
        const std::size_t liveFirst = 10u * kBlockSamples;
        float liveDiff = 0.0f;
        for (std::size_t i = liveFirst; i < kDisengageBlock * kBlockSamples; ++i) {
            liveDiff = std::max(liveDiff, std::fabs(live.left[i] - bypassed.left[i]));
            liveDiff = std::max(liveDiff, std::fabs(live.right[i] - bypassed.right[i]));
        }
        INFO("worst live-wander difference = " << liveDiff);
        REQUIRE(liveDiff > kDepthLiveDiffFloor);

        // ...and after the STATED settle window the skip is exact.
        const std::size_t identityFirst =
            (kDisengageBlock + kDepthIdentityWaitBlocks) * kBlockSamples;
        float worstAfter = 0.0f;
        for (std::size_t i = identityFirst; i < live.left.size(); ++i) {
            worstAfter = std::max(worstAfter, std::fabs(live.left[i] - bypassed.left[i]));
            worstAfter = std::max(worstAfter, std::fabs(live.right[i] - bypassed.right[i]));
        }
        INFO("worst post-disengage difference = " << worstAfter << " (must be exactly 0)");
        CHECK(worstAfter == 0.0f);
    }

    // =========================================================================
    // FR-010a - THE WANDER DISENGAGE DOES NOT STEP THE IMAGE
    // =========================================================================
    // The raw predicate goes false the instant a host writes kFxWanderDepthId = 0,
    // but globalMs_'s width smoother still holds the last modulated width (it
    // advances ONLY inside process(), midside_processor.h:186-192) and the azimuth
    // pair still holds its last gains. Skipping applies EXACT identity, so an
    // unlatched skip steps the stereo image in ONE SAMPLE by the whole residual
    // width error - which at kWanderWidthSpanPercent = 50 is up to half the side
    // signal. The latch keeps the stage running until everything has settled and
    // only then skips.
    //
    // The statistic is SC-008's: the worst |x[i] - x[i-1]| over the transition
    // window against the same statistic over a quiescent window OF THE SAME
    // LENGTH from the SAME render, bounded at 1.5x. Measured at the tap, so the
    // limiter cannot mask a step by chasing it.
    // =========================================================================
    SECTION("the wander disengage does not step the image") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        const TapRender render = captureTapRender(
            fx, kDisengageTotalBlocks, kBlockSamples, [](std::size_t b, Fixture& f) {
                if (b == 0) {
                    // Width and azimuth stay at their defaults, so writing the
                    // wander depth to 0 is by itself enough to make FR-010's raw
                    // predicate false - which is exactly the transition FR-010a
                    // names.
                    f.setParam(Seraphis::kFxWanderDepthId, 1.0);
                    for (const Steinberg::int16 pitch : kEightNoteChord) {
                        f.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
                    }
                } else if (b == kDisengageLiveBlocks) {
                    f.setParam(Seraphis::kFxWanderDepthId, 0.0);
                }
            });

        // The stage really did run and really did disengage.
        CHECK_FALSE(Probe::wanderRunsRaw(*fx.proc));
        CHECK_FALSE(Probe::wanderRunsEffective(*fx.proc));
        CHECK(Probe::midSide(*fx.proc).getWidth()
              == Krate::DSP::MidSideProcessor::kDefaultWidth);

        // The measured window covers the write AND the whole latched settle span
        // that follows it, because an unlatched build steps on the write while a
        // build with a too-short latch steps somewhere inside the span.
        constexpr std::size_t kTestFirst = kDisengageLiveBlocks * kBlockSamples;
        constexpr std::size_t kWindowSamples = kDisengageWindowBlocks * kBlockSamples;
        static_assert(kWindowSamples > static_cast<std::size_t>(kExpectedWanderSettleSamples),
                      "FR-010a: the measured window must outlast the settle countdown");

        // The reference: the same length, from the LIVE-wander stretch, clear of
        // the transition by kDisengageReferenceGap blocks. Taken from THIS render
        // so it carries the same programme material.
        constexpr std::size_t kRefFirst =
            (kDisengageLiveBlocks - kDisengageReferenceGap - kDisengageWindowBlocks)
            * kBlockSamples;

        const float testDelta = maxPerSampleDelta(render, kTestFirst, kWindowSamples);
        const float refDelta = maxPerSampleDelta(render, kRefFirst, kWindowSamples);

        INFO("disengage max per-sample delta = " << testDelta << ", quiescent reference = "
                                                 << refDelta << " (bound " << kDisengageDeltaBound
                                                 << "x)");
        // Non-vacuity: a silent render has no deltas to compare.
        REQUIRE(refDelta > 0.0f);
        CHECK(testDelta <= kDisengageDeltaBound * refDelta);
        CHECK(fx.checkCanaries());
    }
}

// =============================================================================
// SC-002 - THE NEGATIVE CONTROL THE WHOLE PHASE IS BUILT AGAINST
// =============================================================================
// ONE BUILD, ONE PROCESS, ONE Processor INSTANCE. Two renders of an identical
// 10 s MIDI sequence at every registered default - the eight-voice shipped
// polyphony, all 16 effects parameters at their C-6 defaults - differing ONLY in
// whether FR-040's probe skips C-1 steps 4 and 5.
//
// THREE RENDERS RUN, TWO ARE MEASURED. The first is a warm-up whose output is
// discarded: a re-prepare makes this instance REPRODUCIBLE but not VIRGIN, so
// both measured renders are taken after a re-prepare of an already-warmed
// instance. See reprepare()'s banner for the measurement that forces this.
//
// WHY EXACT EQUALITY IS LEGITIMATE HERE AND NOWHERE ELSE IN THIS PHASE. Both
// sides are the SAME COMPILED CODE PATH on the SAME INSTANCE, so codegen is
// identical and the only question asked is whether C-3/C-7's default path is the
// algebraic identity it claims to be (bypassed send, skipped M/S, saturation at
// the same 0.15). This is NOT a checked-in golden and NOT a cross-build
// comparison: a Phase 9-vs-Phase 10 BUILD comparison would demand bit-identical
// floating point across MSVC / GCC / AppleClang, which
// tests/test_helpers/render_fingerprint.h:20-30 measures at 2.9e-5 per sample
// and the project's cross-cutting constraints forbid - and FR-039 makes it
// unimplementable anyway, since seraphis_tests compiles processor.cpp exactly
// once.
//
// THIS CASE MUST STAY GREEN THROUGH EVERY LATER PHASE 10 TASK. If a task makes
// it fail, that task has changed behaviour at the shipped defaults, which
// FR-001 forbids.
// =============================================================================
TEST_CASE("Effects defaults are a no-op on the same build", "[seraphis][effects]") {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    // The script places note traffic through the fixture's own event list - the
    // same object renderBlocks() hands the callback, cleared before every block.
    const auto script = [&fx](std::size_t b, Krate::Test::EventList&,
                              SeraphisTest::ParameterChanges&) {
        if (b == 0) {
            for (const Steinberg::int16 pitch : kEightNoteChord) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
            }
        } else if (b == kNoOpNoteOffBlock) {
            for (const Steinberg::int16 pitch : kEightNoteChord) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, pitch, 0.0f, 0);
            }
        }
    };

    // --- Warm-up: NOT a measured render -------------------------------------
    // BOTH measured renders must be a "second or later" render of this instance,
    // for the reason reprepare()'s banner states and measures: a re-prepare
    // returns the processor to a REPRODUCIBLE state, not to a VIRGIN one, and the
    // difference between the two is ~6.5e-3 - four orders of magnitude above the
    // exact equality SC-002 asserts, and present for a perfectly correct
    // implementation. Warming once and then re-preparing before EACH measured
    // render puts both on the same side of that seam, so the only remaining
    // difference between them is the probe.
    //
    // The warm-up runs the IDENTICAL script, so every sub-component is left in the
    // same converged state the two measured renders will start from.
    fx.renderBlocks(kNoOpRenderBlocks, kBlockSamples, script);
    fx.capturedL.clear();
    fx.capturedR.clear();
    REQUIRE(fx.checkCanaries());

    // --- Render A: probe ENGAGED, C-1 steps 4 and 5 skipped ------------------
    reprepare(fx);
    {
        const ScopedEffectsProbe guard(*fx.proc, /*bypassed*/ true, /*afterOutput*/ false,
                                       /*rampSnap*/ false);
        fx.renderBlocks(kNoOpRenderBlocks, kBlockSamples, script);
    }
    const std::vector<float> bypassedL = fx.capturedL;
    const std::vector<float> bypassedR = fx.capturedR;
    fx.capturedL.clear();
    fx.capturedR.clear();
    REQUIRE(bypassedL.size() == kNoOpRenderBlocks * kBlockSamples);
    REQUIRE(bypassedR.size() == bypassedL.size());
    REQUIRE(fx.checkCanaries());

    // --- Render B: the SAME instance, re-prepared, probe DISENGAGED ----------
    reprepare(fx);
    fx.renderBlocks(kNoOpRenderBlocks, kBlockSamples, script);
    REQUIRE(fx.capturedL.size() == bypassedL.size());
    REQUIRE(fx.capturedR.size() == bypassedR.size());
    REQUIRE(fx.checkCanaries());

    // Non-vacuity: two silences would satisfy exact equality trivially.
    float livePeak = 0.0f;
    for (const float v : fx.capturedL) {
        livePeak = std::max(livePeak, std::fabs(v));
    }
    REQUIRE(livePeak > 0.0f);

    CHECK(maxAbsDiff(bypassedL, fx.capturedL) == 0.0f);
    CHECK(maxAbsDiff(bypassedR, fx.capturedR) == 0.0f);
}

// =============================================================================
// SC-017 - THE EFFECTS SEND IS BLOCK-SIZE INVARIANT
// =============================================================================
// This is the criterion FR-003a's fixed-size accumulator exists to make
// satisfiable. SpectralDelay::process is NOT partition-invariant: its output
// stream position depends on how many analysis frames happened to be ready when
// the call was made, so the SAME audio delivered as one 2048-sample call and as
// four 512-sample calls comes back a whole hop apart - PERMANENTLY, not as a
// start-up transient. The accumulator removes that entirely by calling the
// component with a constant kFxSendChunkSamples = 512 and never with a slice
// length, at the cost of a fixed 512-sample pipeline delay that is identical in
// every partition (plan section 3.1's invariant, fxChunkFill_ + fxOutFill_ ==
// 512 at every slice boundary).
//
// The render is transition-free BY REQUIREMENT: FR-008's reset re-randomizes
// 2 x numBins stereo phases (spectral_delay.h:279-284), and a PARAMETER
// transition lands where the host puts it, so a render carrying one could not
// be partition-invariant for any implementation.
//
// WHAT GATES. The primary gate is the max absolute per-sample difference over
// ALL samples, at render_fingerprint.h's kSampleTolerance (1.0e-4f, :49) - the
// same shape Phase 7 shipped and Phase 9's own SC-008 partition sweep uses
// (unit/midi_event_test.cpp:417-424). compareFingerprints() runs as a
// SECONDARY, WARN-ONLY report for exactly the reason midi_event_test.cpp:426-431
// records: it samples only 32 checkpoints, so it can miss a localised
// divergence, and its kMetricTolerance = 1e-5 relative bound was measured for
// the cross-toolchain spread of the SAME computation, not of a re-partitioned
// one. Making it gate would red a correct implementation.
// =============================================================================
TEST_CASE("The effects send is block-size invariant", "[seraphis][effects]") {
    // --- REQUIRED COVERAGE, ASSERTED NOT ASSUMED -----------------------------
    REQUIRE(std::find(std::begin(kInvariancePartitions), std::end(kInvariancePartitions),
                      kInvarianceReferenceBlock)
            != std::end(kInvariancePartitions));
    // 4096 is the ONLY partition above kMaxBlockSamples, i.e. the only one that
    // enters the slice loop's cap branch.
    REQUIRE(std::size_t{4096} > Krate::DSP::SeraphisEngine::kMaxBlockSamples);
    REQUIRE(std::find(std::begin(kInvariancePartitions), std::end(kInvariancePartitions),
                      std::size_t{4096})
            != std::end(kInvariancePartitions));
    // 1, 2, 3 and 7 all divide into far less than one send chunk, so on those
    // partitions the accumulator carries state across hundreds of calls before
    // it ever runs one.
    for (const std::size_t tiny : {std::size_t{1}, std::size_t{2}, std::size_t{3},
                                   std::size_t{7}}) {
        CAPTURE(tiny);
        REQUIRE(tiny < Seraphis::kFxSendChunkSamples);
        REQUIRE(std::find(std::begin(kInvariancePartitions), std::end(kInvariancePartitions), tiny)
                != std::end(kInvariancePartitions));
    }
    // No script event may coincide with a block boundary in ANY partition, or
    // the MIDI sub-division path would go unexercised for that partition.
    for (const SendScriptEvent& e : kSendScript) {
        for (const std::size_t block : kInvariancePartitions) {
            if (block == 1u) {
                continue;  // every index is a multiple of 1
            }
            CAPTURE(e.position, block);
            REQUIRE(e.position % block != 0u);
        }
    }

    // =========================================================================
    // THE MANDATORY NEGATIVE CONTROL, FIRST - so a vacuous criterion is caught
    // before the expensive sweep runs.
    // =========================================================================
    SECTION("negative control: without the accumulator the partition matters") {
        const RawSendRender reference =
            renderRawSend(std::span<const std::size_t>(kRawSendConstantCall));
        const RawSendRender ragged =
            renderRawSend(std::span<const std::size_t>(kInvariancePartitions));

        REQUIRE(reference.left.size() == kRawSendSamples);
        REQUIRE(ragged.left.size() == kRawSendSamples);

        // Non-vacuity of the control itself: two silences differ by nothing.
        float referencePeak = 0.0f;
        for (const float v : reference.left) {
            referencePeak = std::max(referencePeak, std::fabs(v));
        }
        INFO("raw-send reference peak = " << referencePeak);
        REQUIRE(referencePeak > Krate::DSP::TestUtils::kSampleTolerance);

        const float rawDiffL = maxAbsDiff(ragged.left, reference.left);
        const float rawDiffR = maxAbsDiff(ragged.right, reference.right);
        INFO("raw-send partition divergence: L=" << rawDiffL << " R=" << rawDiffR
                                                 << " (tolerance "
                                                 << Krate::DSP::TestUtils::kSampleTolerance << ")");
        // THE CONTROL: fed raw slice lengths, the component fails the very gate
        // the accumulator makes the plugin pass.
        CHECK(rawDiffL > Krate::DSP::TestUtils::kSampleTolerance);
        CHECK(rawDiffR > Krate::DSP::TestUtils::kSampleTolerance);
    }

    // =========================================================================
    // THE CRITERION
    // =========================================================================
    SECTION("every partition agrees with the reference render") {
        const SendRender reference = renderSendAtBlockSize(kInvarianceReferenceBlock);
        REQUIRE(reference.left.size() == kInvarianceTotalSamples);
        REQUIRE(reference.right.size() == kInvarianceTotalSamples);

        // Non-vacuity: two silences are trivially invariant.
        float referencePeak = 0.0f;
        for (const float v : reference.left) {
            referencePeak = std::max(referencePeak, std::fabs(v));
        }
        CAPTURE(referencePeak);
        REQUIRE(referencePeak > 1.0e-4f);

        for (const std::size_t block : kInvariancePartitions) {
            if (block == kInvarianceReferenceBlock) {
                continue;  // the reference is not compared with itself
            }
            CAPTURE(block);
            const SendRender got = renderSendAtBlockSize(block);
            REQUIRE(got.left.size() == kInvarianceTotalSamples);

            const float dl = maxAbsDiff(got.left, reference.left);
            const float dr = maxAbsDiff(got.right, reference.right);
            INFO("partition " << block << ": worst |diff| L=" << dl << " R=" << dr);
            CHECK(dl <= Krate::DSP::TestUtils::kSampleTolerance);
            CHECK(dr <= Krate::DSP::TestUtils::kSampleTolerance);

            // SECONDARY, WARN-ONLY - see the banner. It MUST NOT gate.
            const auto aggregate = Krate::DSP::TestUtils::compareFingerprints(
                Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(got.left)),
                Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(reference.left)));
            if (!aggregate.withinTolerance()) {
                WARN("SC-017 secondary (non-gating) fingerprint drift at block "
                     << block
                     << ": worstMetricRelativeError=" << aggregate.worstMetricRelativeError
                     << " worstSampleError=" << aggregate.worstSampleError << " "
                     << aggregate.detail);
            }
        }
    }
}

// =============================================================================
// T014 - pushEffectsParams(): EVERY SpectralDelay setter, the tilt compensation,
//        the seeds, and the independence of the two freezes
// =============================================================================
// FR-022, FR-016a, FR-025 and FR-023, measured where they are actually decidable:
// on the COMPONENT'S OWN GETTERS, through the friend probe this TU already holds.
//
// WHY THIS CASE EXISTS AT ALL, stated plainly: every other criterion in this
// phase measures AUDIO, and audio is blind to a missing setter. A build that
// wired setBaseDelayMs and setFeedback and quietly dropped setSpreadMs,
// setDiffusion, setStereoWidth and setSpreadDirection still renders a
// perfectly plausible spectral delay - it passes SC-002 (defaults unchanged),
// SC-005 (it decays), SC-017 (it is partition-invariant) and SC-006 (it is
// under the ceiling). Four registered, automatable controls would simply do
// nothing, and nothing in the suite would say so.
//
// The getters read back the value LAST PUSHED, not a smoother position
// (spectral_delay.h:429, :436, :442, :464, :472, :493, :516, :527, :535 are all
// plain member reads), which is why ONE process() call is enough and no settle
// window is needed.
// =============================================================================
TEST_CASE("Effects parameters reach their components", "[seraphis][effects]") {

    // =========================================================================
    // 1. FR-022 - every registered SpectralDelay setter is actually called
    // =========================================================================
    SECTION("every SpectralDelay setter receives its registered value") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        fx.setParam(Seraphis::kFxDelayTimeId, kFxTimeNormalized);
        fx.setParam(Seraphis::kFxDelaySpreadId, kFxSpreadNormalized);
        fx.setParam(Seraphis::kFxDelaySpreadDirectionId, kFxDirectionNormalized);
        fx.setParam(Seraphis::kFxDelayFeedbackId, kFxFeedbackNormalized);
        fx.setParam(Seraphis::kFxDelayTiltId, kFxTiltNormalized);
        fx.setParam(Seraphis::kFxDelayDiffusionId, kFxDiffusionNormalized);
        fx.setParam(Seraphis::kFxDelayWidthId, kFxDelayWidthNormalized);
        fx.setParam(Seraphis::kFxDelaySyncId, 1.0);
        fx.setParam(Seraphis::kFxDelaySyncNoteId, kFxSyncNoteNormalized);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        const Krate::DSP::SpectralDelay& sd = Probe::spectralDelay(*fx.proc);

        CHECK(sd.getBaseDelayMs() == Catch::Approx(kFxTimePlain).margin(1.0e-4));   // :429
        CHECK(sd.getSpreadMs() == Catch::Approx(kFxSpreadPlain).margin(1.0e-4));    // :436
        // Compared as INTEGERS through the shipped converter, not as enumerators:
        // SpreadDirection's underlying type is std::uint8_t (spectral_delay.h:53),
        // which a stringifier renders as a control CHARACTER, so a failure here
        // would otherwise print unreadably.
        CHECK(Seraphis::fromSpreadDirection(sd.getSpreadDirection())
              == kFxDirectionIndex);                                               // :442
        // THE TILT-COMPENSATED value (FR-016a), never the registered one: the
        // expectation calls the SAME helper the processor must, so the two can
        // only agree if the processor actually used it.
        CHECK(sd.getFeedback()
              == Catch::Approx(Seraphis::tiltCompensatedFeedback(kFxFeedbackPlain, kFxTiltPlain))
                     .margin(1.0e-6));                                             // :464
        CHECK(sd.getFeedbackTilt() == Catch::Approx(kFxTiltPlain).margin(1.0e-6));  // :472
        CHECK(sd.getDiffusion()
              == Catch::Approx(static_cast<float>(kFxDiffusionNormalized)).margin(1.0e-6));  // :493
        CHECK(sd.getStereoWidth()
              == Catch::Approx(static_cast<float>(kFxDelayWidthNormalized)).margin(1.0e-6)); // :516
        // Same reason: TimeMode is also std::uint8_t-backed (note_value.h:29).
        CHECK(static_cast<int>(sd.getTimeMode())
              == static_cast<int>(Krate::DSP::TimeMode::Synced));                  // :527
        CHECK(sd.getNoteValue() == kFxSyncNoteIndex);                              // :535

        // The compensation is NOT the identity at this tilt - otherwise the
        // getFeedback() clause above would pass for a build that ignored FR-016a.
        CHECK(Seraphis::tiltCompensatedFeedback(kFxFeedbackPlain, kFxTiltPlain)
              < kFxFeedbackPlain);
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // 2. FR-022 - ON CHANGE ONLY
    // =========================================================================
    // Every push in this phase is a smoother re-target on a component that is
    // fed a CONSTANT-LENGTH chunk stream; re-pushing an unchanged value every
    // block is a discontinuity for nothing, and the same discipline Phase 9's
    // FR-045 records for setSeed / setPolyphony.
    // =========================================================================
    SECTION("an unchanged value is not re-pushed") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        fx.setParam(Seraphis::kFxDelayTimeId, kFxTimeNormalized);
        fx.setParam(Seraphis::kFxDelaySpreadId, kFxSpreadNormalized);
        fx.setParam(Seraphis::kFxDelayDiffusionId, kFxDiffusionNormalized);
        fx.setParam(Seraphis::kFxWanderRateId, kFxWanderRateNormalized);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        const std::size_t settled = fx.proc->effectsPushCountForTest();
        // Non-vacuity: a counter that never moved would make "unmoved" trivial.
        REQUIRE(settled > std::size_t{0});

        for (std::size_t b = 0; b < kFxUnchangedBlocks; ++b) {
            // The SAME normalized values, re-delivered every block - which is
            // exactly what a host automating a flat lane sends.
            fx.setParam(Seraphis::kFxDelayTimeId, kFxTimeNormalized);
            fx.setParam(Seraphis::kFxDelaySpreadId, kFxSpreadNormalized);
            fx.setParam(Seraphis::kFxDelayDiffusionId, kFxDiffusionNormalized);
            fx.setParam(Seraphis::kFxWanderRateId, kFxWanderRateNormalized);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        }

        CHECK(fx.proc->effectsPushCountForTest() == settled);

        // And a genuine move DOES push - otherwise "unmoved" would also be
        // satisfied by a processor that stopped pushing altogether.
        fx.setParam(Seraphis::kFxDelayTimeId, kFxTimeNormalized * 0.5);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(fx.proc->effectsPushCountForTest() > settled);
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // 3. FR-016a - the tilt compensation, at runtime
    // =========================================================================
    // calculateTiltedFeedback multiplies the pushed feedback by a tilt factor
    // spanning [0, 2] and clamps the PRODUCT to kMaxFeedback = 1.2f
    // (spectral_delay.h:603-614, :99) - NOT to the registered maximum. At
    // feedback 0.95 with tilt +1, 243 of 513 bins would therefore receive a loop
    // gain above unity, and the per-bin recursion tanh(delayedMag * binFeedback)
    // (:751-767) has a stable non-zero fixed point there: those bins sustain
    // FOREVER. The registration cap alone bounds nothing; this is what does.
    // =========================================================================
    SECTION("the feedback is pushed tilt-compensated") {
        // The helper itself, at both worst-case tilts. Exact equality is
        // legitimate: division by 2 is exact in binary floating point, and
        // effects_params.h:95-98 already pins the identical expression at
        // COMPILE time - this clause exists because a processor that ignored the
        // helper would satisfy that static_assert perfectly.
        CHECK(Seraphis::tiltCompensatedFeedback(Seraphis::kFxDelayFeedbackMax, 1.0f)
              == kFxCompensatedAtFullTilt);
        CHECK(Seraphis::tiltCompensatedFeedback(Seraphis::kFxDelayFeedbackMax, -1.0f)
              == kFxCompensatedAtFullTilt);

        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        fx.setParam(Seraphis::kFxDelayFeedbackId, 1.0);  // -> kFxDelayFeedbackMax = 0.95
        fx.setParam(Seraphis::kFxDelayTiltId, 1.0);      // -> +1
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        const Krate::DSP::SpectralDelay& sd = Probe::spectralDelay(*fx.proc);
        CHECK(sd.getFeedbackTilt() == Catch::Approx(Seraphis::kFxDelayTiltMax).margin(1.0e-6));
        CHECK(sd.getFeedback() == Catch::Approx(kFxCompensatedAtFullTilt).margin(1.0e-6));

        // The worst per-bin loop gain the component can now form is
        // compensated * (1 + |tilt|) = the registered maximum, which is < 1.
        const float worstBinGain = sd.getFeedback() * (1.0f + std::fabs(sd.getFeedbackTilt()));
        INFO("worst per-bin loop gain at feedback 0.95 / tilt +1 = " << worstBinGain);
        CHECK(worstBinGain <= Catch::Approx(Seraphis::kFxDelayFeedbackMax).margin(1.0e-6));
        CHECK(worstBinGain < 1.0f);

        // The SAME push after the tilt alone moves: FR-016a's recompute clause.
        // A build that compensated only when the FEEDBACK moved would leave 0.95
        // installed here and put those 243 bins above unity.
        fx.setParam(Seraphis::kFxDelayTiltId, 0.5);  // -> 0.0 tilt
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(sd.getFeedbackTilt() == Catch::Approx(0.0f).margin(1.0e-6));
        CHECK(sd.getFeedback() == Catch::Approx(Seraphis::kFxDelayFeedbackMax).margin(1.0e-6));
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // 4. FR-025 - the rate reaches BOTH drifts
    // =========================================================================
    // C-5: the two drifts share the rate control and differ ONLY by seed salt,
    // so width and azimuth never move in lockstep. A build that pushed the rate
    // to one drift only is invisible in audio - the other simply wanders at its
    // prepared default - so the counter is the component's own getter.
    // =========================================================================
    SECTION("the wander rate reaches both drifts") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        const auto expected = static_cast<float>(kFxWanderRateNormalized);
        // Non-vacuity, asserted rather than assumed: prepare() left both at
        // kDefaultSmoothness, so an unpushed drift reports something else.
        REQUIRE(Probe::widthDrift(*fx.proc).getSmoothness()
                != Catch::Approx(expected).margin(1.0e-6));

        fx.setParam(Seraphis::kFxWanderRateId, kFxWanderRateNormalized);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        CHECK(Probe::widthDrift(*fx.proc).getSmoothness()
              == Catch::Approx(expected).margin(1.0e-6));    // brownian_drift.h:169
        CHECK(Probe::azimuthDrift(*fx.proc).getSmoothness()
              == Catch::Approx(expected).margin(1.0e-6));
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // 5. FR-023 - THE TWO FREEZES ARE INDEPENDENT FEATURES
    // =========================================================================
    // C-4: ID 1204 latches AetherReverb's FDN to unity feedback (an infinite
    // DECAY, aether_reverb.h:2230); ID 1430 CAPTURES SpectralDelay's magnitude/
    // phase spectrum and holds it (spectral_delay.h:479, :677-688). Nothing in
    // this spec couples them, and neither push path may read the other's atomic.
    //
    // NOTE ON WHAT ARM (a) CAN HONESTLY ASSERT. "The Aether freeze must not
    // produce a HELD SEND TAIL" is not an audio measurement: the send is fed the
    // post-Aether bus (C-1 step 4), so a frozen Aether necessarily feeds the send
    // a sustained input and the send's OUTPUT is sustained too - correctly, and
    // for a reason that has nothing to do with the send's own freeze. The
    // decidable question is whether the send was put into HOLD, and the component
    // answers it directly.
    // =========================================================================
    SECTION("the Aether freeze does not engage the spectral freeze") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        REQUIRE(fx.proc->reverbForTest() != nullptr);

        // The send ACTIVE, so "was the send frozen?" is a question with a live
        // subject rather than one asked of a bypassed component.
        fx.setParam(Seraphis::kFxDelayMixId, kInvMixNormalized);
        for (const Steinberg::int16 pitch : kEightNoteChord) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
        }
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        fx.renderBlocks(kIndepLatchBlocks, kBlockSamples);

        // ONLY ID 1204 moves. ID 1430 is untouched at its C-6 default (off).
        fx.setParam(Seraphis::kAetherFreezeId, 1.0);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        fx.renderBlocks(kIndepLatchBlocks, kBlockSamples);

        // Non-vacuity: the Aether freeze really did engage (isFrozen() is false
        // until the 50 ms latch COMPLETES, aether_reverb.h:2488-2495), and the
        // send really did run.
        REQUIRE(fx.proc->reverbForTest()->isFrozen());
        REQUIRE(fx.proc->sendChunkCountForTest() > std::size_t{0});

        // THE CLAUSE: the spectral freeze is untouched.
        CHECK_FALSE(Probe::spectralDelay(*fx.proc).isFreezeEnabled());
        CHECK(fx.checkCanaries());
    }

    SECTION("the spectral freeze does not engage the Aether freeze") {
        Fixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        REQUIRE(fx.proc->reverbForTest() != nullptr);

        // C-1 steps 4 and 5 are skipped for this arm, deliberately: with the send
        // out of the signal the measured decay is the AETHER's alone, so the
        // clause stays valid once ID 1430 really does hold the send's spectrum
        // (T016) - at which point a total-output decay assertion would be
        // measuring the send and would be wrong.
        const ScopedEffectsProbe guard(*fx.proc, /*bypassed*/ true, /*afterOutput*/ false,
                                       /*rampSnap*/ false);

        const auto script = [&fx](std::size_t b, Krate::Test::EventList&,
                                  SeraphisTest::ParameterChanges& params) {
            if (b == 0) {
                // ONLY ID 1430 moves. ID 1204 is untouched at its default (off).
                params.addQueue(Seraphis::kFxSpectralFreezeId).addTestPoint(0, 1.0);
                for (const Steinberg::int16 pitch : kEightNoteChord) {
                    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
                }
            } else if (b == kIndepNoteOffBlock) {
                for (const Steinberg::int16 pitch : kEightNoteChord) {
                    fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, pitch, 0.0f, 0);
                }
            }
        };
        fx.renderBlocks(kIndepTotalBlocks, kBlockSamples, script);
        REQUIRE(fx.capturedL.size() == kIndepTotalBlocks * kBlockSamples);
        REQUIRE(fx.checkCanaries());

        // THE CLAUSE: the Aether is not frozen.
        CHECK_FALSE(fx.proc->reverbForTest()->isFrozen());

        // And its tail is still DECAYING, which is the audible half of the same
        // statement: a coupled implementation would hold it flat.
        const float early = windowRms(fx.capturedL, kIndepEarlyBlock, kIndepWindowBlocks);
        const float late = windowRms(fx.capturedL, kIndepLateBlock, kIndepWindowBlocks);
        INFO("Aether tail RMS: early=" << early << " late=" << late);
        REQUIRE(early > 0.0f);  // non-vacuity: silence decays trivially
        CHECK(late < kIndepDecayFactor * early);
    }
}

// =============================================================================
// T015 - SC-005 AND SC-019: THE ISOLATED SEND RETURN, MEASURED AT THE TAP
// =============================================================================
// Both criteria measure spec SC-003's ISOLATED SEND RETURN, whose definition
// they inherit verbatim:
//
//   render(kFxDelayMixId = 1) - render(kFxDelayMixId = 0), same MIDI script,
//   same seed, read from FR-041 clause 6's preOutputTapForTest(), i.e. from C-1
//   step 5's output BEFORE engine_->processOutputStage().
//
// Reading at the tap is what makes the difference LINEAR in the quantity being
// measured: both renders otherwise pass through TapeSaturator + TruePeakLimiter
// (processor.cpp:1170), so a difference of two summed buses would be a
// difference of two NONLINEARLY-SHAPED signals - and a 60 dB decay measured
// through a limiter is not a 60 dB decay of the send. Every render below
// therefore uses kBlock = 512 (<= 2048) and asserts preOutputTapTruncatedForTest()
// is false on EVERY block, per that same definition (plan D-8 clause 3).
//
// TWO FRESH FIXTURES PER PAIR, NOT ONE RE-PREPARED INSTANCE. reprepare()'s banner
// records the measurement: a re-prepared instance is REPRODUCIBLE but not VIRGIN
// and diverges from a virgin one by ~6.5e-3, while "two fresh Processor instances
// agree at 0.0f". Both halves of every pair here are therefore virgin renders,
// which is the only shape under which the difference is the send's contribution
// and nothing else. Only one engine is alive at a time (each fixture is scoped),
// so the ~33 MB of per-voice capture rings is paid once, not twice.
// =============================================================================
namespace {

/// The `-ffast-math` rule, and it is NOT waived by this TU's -fno-fast-math
/// entry (tests/CMakeLists.txt:105): std::isnan / std::isinf /
/// std::numeric_limits<>::infinity() are forbidden repo-wide in DSP assertions
/// because the macOS leg may assume finiteness and fold the test away. A
/// non-finite float is exactly "all eight exponent bits set" - an INTEGER test
/// on the bit pattern, which no flag can reshape.
[[nodiscard]] bool isNonFiniteBits(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

/// One tap-measuring render. `script(blockIndex, fixture)` places automation and
/// note traffic through the fixture BEFORE the block (processBlock() clears both
/// queues afterwards); `sink(blockIndex, tapL, tapR)` receives THIS block's
/// pre-output-stage tap.
///
/// The three per-block REQUIREs are the isolated-return definition's own
/// preconditions, not decoration: a short or truncated tap would silently make
/// every measurement below cover less audio than it claims to.
template <typename Script, typename Sink>
void renderTapBlocks(Fixture& fx, std::size_t numBlocks, const Script& script, const Sink& sink) {
    for (std::size_t b = 0; b < numBlocks; ++b) {
        script(b, fx);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        const std::span<const float> tapL = fx.proc->preOutputTapLForTest();
        const std::span<const float> tapR = fx.proc->preOutputTapRForTest();
        REQUIRE(tapL.size() == kBlockSamples);
        REQUIRE(tapR.size() == kBlockSamples);
        REQUIRE_FALSE(fx.proc->preOutputTapTruncatedForTest());

        sink(b, tapL, tapR);
    }
    REQUIRE(fx.checkCanaries());
}

// =============================================================================
// SC-005 - THE SPECTRAL DELAY DECAYS AT ITS REGISTERED MAXIMUM, AT BOTH TILTS
// =============================================================================
// Clause 1 is arithmetic and needs no render; clauses 2 and 3 are two long ones.
//
// WHY THE DELAY TIME IS PINNED AT kDefaultDelayMs = 250 ms. Feedback applies
// ONCE PER DELAY-LINE TRAVERSAL (spectral_delay.h:751-767), so the criterion is
// delay-time dependent: 120 s at a worst-bin loop gain of 0.95 is 480
// traversals at 250 ms => 480 * 20*log10(0.95) = -213.8 dB, but only -53 dB at
// 1000 ms (120 traversals) and -26.7 dB at kMaxDelayMs = 2000 ms (60). An
// unpinned delay time would make a 60 dB requirement unsatisfiable at the top of
// the registered range for a perfectly correct implementation.

/// numBins at kDefaultFFTSize, i.e. the b/512 denominator in clause 1's formula
/// and in calculateTiltedFeedback (spectral_delay.h:609-610).
constexpr std::size_t kDecayBinCount = Krate::DSP::SpectralDelay::kDefaultFFTSize / 2u + 1u;
static_assert(kDecayBinCount == 513u, "SC-005 clause 1 sweeps 513 bins at fftSize 1024");

/// 94 * 512 = 48 128 >= 48 000, so every window below is at least the nominal
/// duration it is named for. (Same rounding kIndepBlocksPerSecond uses.)
constexpr std::size_t kDecayBurstBlocks = kIndepBlocksPerSecond;              // ~1 s
constexpr std::size_t kDecayTailBlocks = 120u * kIndepBlocksPerSecond;        // ~120 s
constexpr std::size_t kDecayTotalBlocks = kDecayBurstBlocks + kDecayTailBlocks;
/// ~0.5 s. The RMS series is measured at THIS resolution and the 5 s shape
/// windows are aggregated from it, so the PEAK the 60 dB is referenced to is the
/// burst's own peak rather than a burst diluted across five seconds.
constexpr std::size_t kDecayWindowBlocks = 47;
static_assert(kDecayTotalBlocks % kDecayWindowBlocks == 0u,
              "SC-005: the render must be a whole number of RMS windows");
constexpr std::size_t kDecayWindowCount = kDecayTotalBlocks / kDecayWindowBlocks;  // 242
/// 10 x ~0.5 s = the criterion's ~5 s shape window.
constexpr std::size_t kDecayShapeSpan = 10;
constexpr std::size_t kDecayShapeCount = kDecayWindowCount / kDecayShapeSpan;      // 24

/// kDefaultDelayMs = 250 through the registered range: 0 + 0.125 * 2000. 0.125 is
/// exact in binary floating point, so the pinned time is EXACTLY the constant.
constexpr double kDecayDelayNormalized =
    static_cast<double>(Krate::DSP::SpectralDelay::kDefaultDelayMs - Seraphis::kFxDelayTimeMinMs)
    / static_cast<double>(Seraphis::kFxDelayTimeMaxMs - Seraphis::kFxDelayTimeMinMs);
static_assert(static_cast<float>(Seraphis::kFxDelayTimeMinMs
                                 + kDecayDelayNormalized
                                       * (Seraphis::kFxDelayTimeMaxMs - Seraphis::kFxDelayTimeMinMs))
                  == Krate::DSP::SpectralDelay::kDefaultDelayMs,
              "SC-005 clause 2: the pinned delay time IS kDefaultDelayMs");

constexpr double kDecayFeedbackNormalized = 1.0;   // -> kFxDelayFeedbackMax = 0.95
constexpr double kDecayDiffusionNormalized = 1.0;  // a plain unit row
constexpr double kDecaySpreadNormalized = 0.0;     // spread 0 - one delay, every bin
/// The two tilt EXTREMES, derived from the registered range rather than typed:
/// normalized 0 -> kFxDelayTiltMin, normalized 1 -> kFxDelayTiltMax.
constexpr double kDecayTiltLowNormalized = 0.0;
constexpr double kDecayTiltHighNormalized = 1.0;
static_assert(Seraphis::kFxDelayTiltMin == -1.0f && Seraphis::kFxDelayTiltMax == 1.0f,
              "SC-005 clause 2 measures BOTH tilt extremes");

/// 10^(-60/20). The criterion's "falls >= 60 dB below its peak".
constexpr double kDecay60dB = 1.0e-3;
/// 10^(+0.5/20). Clause 3 is a TOLERANCE, not strict monotonicity: a dispersive
/// per-bin decay with diffusion at 1.0 is not guaranteed monotone.
constexpr double kDecayShapeRiseRatio = 1.0592537;
/// An absolute floor beside the ratio, so two windows that have both reached
/// silence cannot produce an enormous dB "rise" out of nothing. -180 dBFS.
constexpr double kDecayShapeFloor = 1.0e-9;
/// Non-vacuity: two silences decay trivially.
constexpr double kDecayReturnPeakFloor = 1.0e-4;
/// The EXCITATION must itself stop, or "the send decayed" is unfalsifiable - a
/// still-live bus keeps feeding the send and no correct implementation could
/// meet the criterion. Same 60 dB shape as the criterion itself.
constexpr double kDecayBusStopRatio = 1.0e-3;

/// SC-005's script. Everything is placed at block 0 (offset 0) so the operating
/// point is in force before a single sample is rendered.
///
/// kAtmosLevelId IS DRIVEN TO 0, and that is isolation rather than relaxation:
/// the criterion's subject is the SEND's decay, and the granular atmosphere layer
/// is a sustaining source by design (kAtmosGrainSecondsDefault = 4.0,
/// kAtmosDensityDefault = 4.0 gr/s). Left at its 0.5 default it would keep the
/// bus - and therefore the send's INPUT - alive for the whole 120 s, which would
/// confound "the send sustains" with "its input never stopped". Every other
/// parameter, the Aether included (4 s decay, i.e. gone ~100 s before the
/// measurement ends), stays at its shipped default.
void applyDecayScript(std::size_t b, Fixture& fx, double mixNormalized, double tiltNormalized) {
    if (b == 0) {
        fx.setParam(Seraphis::kFxDelayMixId, mixNormalized);
        fx.setParam(Seraphis::kFxDelayTimeId, kDecayDelayNormalized);
        fx.setParam(Seraphis::kFxDelaySpreadId, kDecaySpreadNormalized);
        fx.setParam(Seraphis::kFxDelayFeedbackId, kDecayFeedbackNormalized);
        fx.setParam(Seraphis::kFxDelayTiltId, tiltNormalized);
        fx.setParam(Seraphis::kFxDelayDiffusionId, kDecayDiffusionNormalized);
        fx.setParam(Seraphis::kAtmosLevelId, 0.0);
        for (const Steinberg::int16 pitch : kEightNoteChord) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
        }
    } else if (b == kDecayBurstBlocks) {
        for (const Steinberg::int16 pitch : kEightNoteChord) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, pitch, 0.0f, 0);
        }
    }
}

struct DecayArm {
    std::vector<double> returnRms;  ///< isolated return, per ~0.5 s window
    std::vector<double> busRms;     ///< the bus alone, same windows
    std::size_t nonFiniteSamples = 0;
    std::size_t sendChunks = 0;
};

/// Both halves of one tilt arm. Render A (the send bypassed) is kept whole so
/// render B can be differenced against it SAMPLE BY SAMPLE while it streams -
/// which is why only one 121 s render is ever resident.
[[nodiscard]] DecayArm runDecayArm(double tiltNormalized) {
    constexpr std::size_t kWindowSamples = kDecayWindowBlocks * kBlockSamples;
    // x2: both channels contribute to one RMS.
    constexpr double kWindowDivisor = static_cast<double>(2u * kWindowSamples);

    DecayArm arm;
    std::vector<double> busSumSq(kDecayWindowCount, 0.0);
    std::vector<double> returnSumSq(kDecayWindowCount, 0.0);

    std::vector<float> busL;
    std::vector<float> busR;
    busL.reserve(kDecayTotalBlocks * kBlockSamples);
    busR.reserve(kDecayTotalBlocks * kBlockSamples);

    // ---- render A: kFxDelayMixId = 0 ---------------------------------------
    {
        Fixture bypassed;
        REQUIRE(bypassed.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        renderTapBlocks(
            bypassed, kDecayTotalBlocks,
            [tiltNormalized](std::size_t b, Fixture& f) {
                applyDecayScript(b, f, 0.0, tiltNormalized);
            },
            [&](std::size_t b, std::span<const float> l, std::span<const float> r) {
                busL.insert(busL.end(), l.begin(), l.end());
                busR.insert(busR.end(), r.begin(), r.end());
                double acc = 0.0;
                for (std::size_t i = 0; i < kBlockSamples; ++i) {
                    acc += static_cast<double>(l[i]) * static_cast<double>(l[i])
                           + static_cast<double>(r[i]) * static_cast<double>(r[i]);
                }
                busSumSq[b / kDecayWindowBlocks] += acc;
            });
        // FR-007: at mix 0 the send is bypassed and must not run at all.
        REQUIRE(bypassed.proc->sendChunkCountForTest() == std::size_t{0});
    }

    // ---- render B: kFxDelayMixId = 1, differenced as it streams -------------
    {
        Fixture active;
        REQUIRE(active.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        renderTapBlocks(
            active, kDecayTotalBlocks,
            [tiltNormalized](std::size_t b, Fixture& f) {
                applyDecayScript(b, f, 1.0, tiltNormalized);
            },
            [&](std::size_t b, std::span<const float> l, std::span<const float> r) {
                const std::size_t base = b * kBlockSamples;
                double acc = 0.0;
                for (std::size_t i = 0; i < kBlockSamples; ++i) {
                    const float dl = l[i] - busL[base + i];
                    const float dr = r[i] - busR[base + i];
                    if (isNonFiniteBits(l[i]) || isNonFiniteBits(r[i]) || isNonFiniteBits(dl)
                        || isNonFiniteBits(dr)) {
                        ++arm.nonFiniteSamples;
                    }
                    acc += static_cast<double>(dl) * static_cast<double>(dl)
                           + static_cast<double>(dr) * static_cast<double>(dr);
                }
                returnSumSq[b / kDecayWindowBlocks] += acc;
            });
        arm.sendChunks = active.proc->sendChunkCountForTest();
    }

    arm.returnRms.reserve(kDecayWindowCount);
    arm.busRms.reserve(kDecayWindowCount);
    for (std::size_t w = 0; w < kDecayWindowCount; ++w) {
        arm.returnRms.push_back(std::sqrt(returnSumSq[w] / kWindowDivisor));
        arm.busRms.push_back(std::sqrt(busSumSq[w] / kWindowDivisor));
    }
    return arm;
}

/// Clause 3's ~5 s window, aggregated from ten equal ~0.5 s ones - equal sizes,
/// so the energy mean is exact rather than approximate.
[[nodiscard]] double decayShapeRms(const std::vector<double>& rmsSeries, std::size_t shapeIndex) {
    double acc = 0.0;
    for (std::size_t i = 0; i < kDecayShapeSpan; ++i) {
        const double v = rmsSeries[shapeIndex * kDecayShapeSpan + i];
        acc += v * v;
    }
    return std::sqrt(acc / static_cast<double>(kDecayShapeSpan));
}

/// The whole assertion set for one tilt extreme, so the two arms cannot drift
/// apart by being written twice.
void checkDecayArm(double tiltNormalized, const char* what) {
    // BEFORE the render, so a failure inside it is labelled too.
    INFO("tilt arm: " << what);
    const DecayArm arm = runDecayArm(tiltNormalized);

    // The send really ran, and produced nothing non-finite.
    REQUIRE(arm.sendChunks > std::size_t{0});
    CHECK(arm.nonFiniteSamples == std::size_t{0});

    double peak = 0.0;
    for (const double v : arm.returnRms) {
        peak = std::max(peak, v);
    }
    const double finalRms = arm.returnRms.back();
    INFO("isolated-return RMS: peak=" << peak << " final=" << finalRms
                                      << " (required <= " << peak * kDecay60dB << ")");

    // Non-vacuity: a send that produced silence would "decay" trivially.
    REQUIRE(peak > kDecayReturnPeakFloor);

    // PRECONDITION, not a criterion: the excitation itself stopped. If the bus
    // were still live at t = 120 s the send would be correctly reproducing it and
    // no implementation could pass - the failure would be in this script, not in
    // the compensation.
    double busPeak = 0.0;
    for (const double v : arm.busRms) {
        busPeak = std::max(busPeak, v);
    }
    INFO("bus RMS: peak=" << busPeak << " final=" << arm.busRms.back());
    REQUIRE(busPeak > kDecayReturnPeakFloor);
    REQUIRE(arm.busRms.back() < busPeak * kDecayBusStopRatio);

    // --- CLAUSE 2: >= 60 dB inside 120 s = 480 traversals of the 250 ms delay -
    CHECK(finalRms <= peak * kDecay60dB);

    // --- CLAUSE 3: the shape never climbs more than 0.5 dB --------------------
    for (std::size_t k = 1; k < kDecayShapeCount; ++k) {
        const double previous = decayShapeRms(arm.returnRms, k - 1);
        const double current = decayShapeRms(arm.returnRms, k);
        CAPTURE(k, previous, current);
        CHECK(current <= previous * kDecayShapeRiseRatio + kDecayShapeFloor);
    }
}

// =============================================================================
// SC-019 - TEMPO SYNC PRODUCES THE TEMPO-DERIVED PERIOD
// =============================================================================
// The measured quantity is the ECHO SPACING of the isolated return, recovered
// from the AUTOCORRELATION OF ITS ENVELOPE.
//
// WHY THE SPACING AND NOT THE FIRST ARRIVAL. An arrival time would have to be
// read through the component's own analysis/synthesis smearing (a 1024-sample
// Hann pair) and would inherit whatever bias that leaves - up to ~21 ms against a
// +-10.67 ms tolerance. Successive echoes are smeared IDENTICALLY, so their
// SPACING carries no such bias: for h = sum_k a^(k-1) delta(t - L - kT) the
// envelope autocorrelation is R_hh * R_xx with R_hh(mT) = a^m / (1 - a^2),
// strictly decreasing in m, so the argmax over lags above the excitation's own
// correlation width lands at exactly T.
//
// WHY THE EXCITATION IS A SHORT BURST WITH THE SUSTAINING LAYERS OFF. The
// envelope of a SUSTAINED input has no periodic structure to correlate - the
// method needs the input to stop inside one delay period. kAetherMixId,
// kAtmosLevelId, kBodyMixId and kBodyCloudMixId go to 0 and kCloudAttackId /
// kEnvReleaseMsId to their minima so the bus is a ~64 ms burst, shorter than the
// SHORTEST period measured below (107.14 ms at 140 BPM, index 7). The case
// ASSERTS that the bus really did stop, so a mis-shaped excitation fails loudly
// instead of mismeasuring quietly.
//
// WHY THERE IS A 22 s SETTLE BEFORE THE BURST. SpectralDelay's base-delay
// smoother is configured with a per-SAMPLE 50 ms coefficient (spectral_delay.h:
// 184-194) but advanced exactly ONCE PER SPECTRAL FRAME (:646), i.e. once per
// 512 samples: coeff = exp(-5000/(50*48000)) = 0.997919 per advance, 93.75
// advances per second => a time constant of ~5.12 s. prepare() snaps it to the
// component's own kDefaultDelayMs = 250 ms (:197, :206), so a synced target of
// 107-222 ms is approached over tens of seconds. 22 s is >= 4.3 time constants,
// leaving under 3 ms of the largest initial 143 ms error - well inside the
// +-512-sample tolerance. That the START point is 250 ms is also what keeps the
// criterion DISCRIMINATING: 250 ms is >= 1333 samples away from every expected
// period below, so a build that ignored the transport entirely fails every arm.

/// hop of the RMS envelope. 64 samples = 1.33 ms, i.e. eight times finer than
/// the criterion's tolerance, so quantisation cannot consume it.
constexpr std::size_t kSyncEnvHop = 64;
/// The searched lag band, in samples: 62.5 ms .. 333 ms. It deliberately spans
/// EVERY value a wrong implementation could produce - the 120 BPM fallback
/// (125 ms), the free-mode delay time (250 ms) and the other registered index -
/// so the argmax is not narrowed onto the expected answer.
constexpr std::size_t kSyncMinLagSamples = 3000;
constexpr std::size_t kSyncMaxLagSamples = 16000;
constexpr std::size_t kSyncMinLagHops = kSyncMinLagSamples / kSyncEnvHop;
constexpr std::size_t kSyncMaxLagHops = kSyncMaxLagSamples / kSyncEnvHop;
/// "+- one hop", and it IS the send's hop rather than a transcription of it.
constexpr std::size_t kSyncToleranceSamples = Seraphis::kFxSendChunkSamples;
static_assert(kSyncToleranceSamples == 512u, "SC-019: +- one hop = +- 512 samples");

constexpr std::size_t kSyncSettleBlocks = 22u * kIndepBlocksPerSecond;  // ~22 s
constexpr std::size_t kSyncNoteBlocks = 6;                              // ~64 ms burst
constexpr std::size_t kSyncMeasureBlocks = 300;                         // ~3.2 s
constexpr std::size_t kSyncTotalBlocks = kSyncSettleBlocks + kSyncMeasureBlocks;

/// 0.70 pushed. Tilt stays at its 0 default, so tiltCompensatedFeedback is the
/// identity here and every bin gets 0.70 - enough repeats (1, .7, .49, .34, ...)
/// for the autocorrelation to have several echo PAIRS to work with.
constexpr double kSyncFeedbackPlain = 0.70;
constexpr double kSyncFeedbackNormalized =
    kSyncFeedbackPlain / static_cast<double>(Seraphis::kFxDelayFeedbackMax);

/// The excitation must have decayed to this fraction of its own burst RMS before
/// the measurement region ends, or the envelope has no echo structure to find.
constexpr double kSyncBusStopRatio = 0.03;  // -30 dB
constexpr double kSyncReturnFloor = 1.0e-5;

struct SyncConfig {
    const char* name;
    bool useContext;       ///< false -> clearProcessContext(), i.e. NO transport
    bool tempoValid;       ///< ProcessContext::kTempoValid
    double contextTempo;   ///< what the host puts in ProcessContext::tempo
    double expectedTempo;  ///< the tempo the send MUST actually use
    int noteIndex;         ///< kFxDelaySyncNoteId
};

/// One of these is the registered default 7 ("1/16"), which SC-019 requires. The
/// last two are FR-030's discriminating pair: the second one leaves a STALE,
/// NON-ZERO tempo in the context with kTempoValid CLEAR, which the component's
/// own `tempo <= 0.0` guard (spectral_delay.h:325-327) would happily sync to.
constexpr SyncConfig kSyncedConfigs[] = {
    {.name = "90 BPM, index 7 (1/16)", .useContext = true, .tempoValid = true, .contextTempo = 90.0, .expectedTempo = 90.0, .noteIndex = 7},
    {.name = "90 BPM, index 9 (1/8T)", .useContext = true, .tempoValid = true, .contextTempo = 90.0, .expectedTempo = 90.0, .noteIndex = 9},
    {.name = "140 BPM, index 7 (1/16)", .useContext = true, .tempoValid = true, .contextTempo = 140.0, .expectedTempo = 140.0, .noteIndex = 7},
    {.name = "140 BPM, index 9 (1/8T)", .useContext = true, .tempoValid = true, .contextTempo = 140.0, .expectedTempo = 140.0, .noteIndex = 9},
};

constexpr SyncConfig kSyncFallbackConfigs[] = {
    {.name = "no ProcessContext at all", .useContext = false, .tempoValid = false, .contextTempo = 0.0, .expectedTempo = 120.0, .noteIndex = 7},
    {.name = "stale tempo 90 with kTempoValid CLEAR", .useContext = true, .tempoValid = false, .contextTempo = 90.0, .expectedTempo = 120.0, .noteIndex = 7},
};

constexpr int kSyncDefaultIndex = Seraphis::kFxDelaySyncNoteDefaultIndex;
static_assert(kSyncedConfigs[0].noteIndex == kSyncDefaultIndex,
              "SC-019: one measured index MUST be the registered default 7");

/// The registered dropdown row, derived from the shipped table's own size.
[[nodiscard]] double syncNoteNormalized(int index) noexcept {
    return static_cast<double>(index)
           / static_cast<double>(Seraphis::kFxDelaySyncNoteLabels.size() - 1u);
}

/// Code-unit comparison against a narrow literal, so the assertion is
/// independent of whether Steinberg::Vst::TChar is char16_t or unsigned short on
/// a given leg.
[[nodiscard]] bool labelEquals(const Steinberg::Vst::TChar* label, const char* ascii) noexcept {
    for (std::size_t i = 0;; ++i) {
        const auto expected = static_cast<std::uint16_t>(static_cast<unsigned char>(ascii[i]));
        const auto actual = static_cast<std::uint16_t>(label[i]);
        if (actual != expected) {
            return false;
        }
        if (expected == 0u) {
            return true;
        }
    }
}

void applySyncScript(std::size_t b, Fixture& fx, const SyncConfig& cfg, double mixNormalized) {
    if (b == 0) {
        fx.setParam(Seraphis::kFxDelayMixId, mixNormalized);
        fx.setParam(Seraphis::kFxDelaySyncId, 1.0);
        fx.setParam(Seraphis::kFxDelaySyncNoteId, syncNoteNormalized(cfg.noteIndex));
        fx.setParam(Seraphis::kFxDelayFeedbackId, kSyncFeedbackNormalized);
        fx.setParam(Seraphis::kFxDelaySpreadId, 0.0);
        // The excitation-shaping set - see the banner. Every one of these is
        // driven to the MINIMUM of its own registered range.
        fx.setParam(Seraphis::kAetherMixId, 0.0);
        fx.setParam(Seraphis::kAtmosLevelId, 0.0);
        fx.setParam(Seraphis::kBodyMixId, 0.0);
        fx.setParam(Seraphis::kBodyCloudMixId, 0.0);
        fx.setParam(Seraphis::kCloudAttackId, 0.0);
        fx.setParam(Seraphis::kEnvReleaseMsId, 0.0);
    } else if (b == kSyncSettleBlocks) {
        for (const Steinberg::int16 pitch : kEightNoteChord) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
        }
    } else if (b == kSyncSettleBlocks + kSyncNoteBlocks) {
        for (const Steinberg::int16 pitch : kEightNoteChord) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, pitch, 0.0f, 0);
        }
    }
}

struct SyncRender {
    std::vector<float> left, right;
    std::size_t sendChunks = 0;
};

[[nodiscard]] SyncRender renderSyncArm(const SyncConfig& cfg, double mixNormalized) {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    // The transport, EXACTLY as the config describes it. clearProcessContext()
    // is the fixture's own default, restated so the arm reads as a decision.
    if (cfg.useContext) {
        fx.setTempo(cfg.contextTempo, 4, 4, cfg.tempoValid, true);
    } else {
        fx.clearProcessContext();
    }

    SyncRender out;
    out.left.reserve(kSyncTotalBlocks * kBlockSamples);
    out.right.reserve(kSyncTotalBlocks * kBlockSamples);

    renderTapBlocks(
        fx, kSyncTotalBlocks,
        [&cfg, mixNormalized](std::size_t b, Fixture& f) {
            applySyncScript(b, f, cfg, mixNormalized);
        },
        [&out](std::size_t, std::span<const float> l, std::span<const float> r) {
            out.left.insert(out.left.end(), l.begin(), l.end());
            out.right.insert(out.right.end(), r.begin(), r.end());
        });

    out.sendChunks = fx.proc->sendChunkCountForTest();
    return out;
}

/// Per-hop RMS of the ISOLATED RETURN over [begin, end), both channels.
[[nodiscard]] std::vector<double> isolatedReturnEnvelope(const SyncRender& bus,
                                                         const SyncRender& withSend,
                                                         std::size_t begin,
                                                         std::size_t& nonFinite) {
    const std::size_t end = std::min(bus.left.size(), withSend.left.size());
    std::vector<double> env;
    if (begin >= end) {
        return env;
    }
    env.reserve((end - begin) / kSyncEnvHop);
    for (std::size_t start = begin; start + kSyncEnvHop <= end; start += kSyncEnvHop) {
        double acc = 0.0;
        for (std::size_t i = start; i < start + kSyncEnvHop; ++i) {
            const float dl = withSend.left[i] - bus.left[i];
            const float dr = withSend.right[i] - bus.right[i];
            if (isNonFiniteBits(dl) || isNonFiniteBits(dr)) {
                ++nonFinite;
            }
            acc += static_cast<double>(dl) * static_cast<double>(dl)
                   + static_cast<double>(dr) * static_cast<double>(dr);
        }
        env.push_back(std::sqrt(acc / static_cast<double>(2u * kSyncEnvHop)));
    }
    return env;
}

[[nodiscard]] double rangeRms(const std::vector<float>& l, const std::vector<float>& r,
                              std::size_t begin, std::size_t end) {
    const std::size_t hi = std::min({end, l.size(), r.size()});
    if (begin >= hi) {
        return 0.0;
    }
    double acc = 0.0;
    for (std::size_t i = begin; i < hi; ++i) {
        acc += static_cast<double>(l[i]) * static_cast<double>(l[i])
               + static_cast<double>(r[i]) * static_cast<double>(r[i]);
    }
    return std::sqrt(acc / static_cast<double>(2u * (hi - begin)));
}

/// argmax of the MEAN-REMOVED autocorrelation over [minLagHops, maxLagHops].
///
/// Mean removal is what stops a decaying pedestal from dragging the argmax to the
/// bottom of the band; the estimator is deliberately the BIASED one (no per-lag
/// re-normalisation), whose gentle taper toward long lags is <= 10 % across this
/// band and which does not amplify tail noise the way the unbiased form does.
[[nodiscard]] std::size_t dominantPeriodHops(const std::vector<double>& env,
                                             double& bestScore, double& zeroLagScore) {
    bestScore = 0.0;
    zeroLagScore = 0.0;
    if (env.size() <= kSyncMinLagHops + 1u) {
        return 0;
    }

    double mean = 0.0;
    for (const double v : env) {
        mean += v;
    }
    mean /= static_cast<double>(env.size());

    std::vector<double> centred(env.size(), 0.0);
    for (std::size_t i = 0; i < env.size(); ++i) {
        centred[i] = env[i] - mean;
        zeroLagScore += centred[i] * centred[i];
    }

    const std::size_t hi = std::min(kSyncMaxLagHops, env.size() - 1u);
    std::size_t bestLag = kSyncMinLagHops;
    bool first = true;
    for (std::size_t lag = kSyncMinLagHops; lag <= hi; ++lag) {
        double acc = 0.0;
        for (std::size_t i = 0; i + lag < centred.size(); ++i) {
            acc += centred[i] * centred[i + lag];
        }
        if (first || acc > bestScore) {
            bestScore = acc;
            bestLag = lag;
            first = false;
        }
    }
    return bestLag;
}

/// One measured arm: two virgin renders, differenced at the tap, one period.
void checkSyncConfig(const SyncConfig& cfg) {
    INFO("SC-019 arm: " << cfg.name);

    const float expectedMs =
        Krate::DSP::dropdownToDelayMs(cfg.noteIndex, cfg.expectedTempo);  // note_value.h:259
    const auto expectedSamples =
        static_cast<double>(expectedMs) * 0.001 * kSampleRate;
    INFO("expected period = " << expectedMs << " ms = " << expectedSamples << " samples");

    // The band must be able to express both the right answer and the wrong ones.
    REQUIRE(expectedSamples > static_cast<double>(kSyncMinLagSamples));
    REQUIRE(expectedSamples < static_cast<double>(kSyncMaxLagSamples));

    const SyncRender bus = renderSyncArm(cfg, 0.0);
    REQUIRE(bus.sendChunks == std::size_t{0});  // FR-007, at mix 0
    const SyncRender withSend = renderSyncArm(cfg, 1.0);
    REQUIRE(withSend.sendChunks > std::size_t{0});
    REQUIRE(bus.left.size() == withSend.left.size());

    const std::size_t measureBegin = kSyncSettleBlocks * kBlockSamples;
    const std::size_t burstEnd = measureBegin + (kSyncNoteBlocks + 4u) * kBlockSamples;
    const std::size_t tailBegin = bus.left.size() - kIndepBlocksPerSecond * kBlockSamples;

    // PRECONDITION: the excitation is a BURST and it really stopped. Without
    // this the envelope has no echo structure and the argmax below would be
    // measuring noise.
    const double busBurstRms = rangeRms(bus.left, bus.right, measureBegin, burstEnd);
    const double busTailRms = rangeRms(bus.left, bus.right, tailBegin, bus.left.size());
    INFO("bus burst RMS=" << busBurstRms << " tail RMS=" << busTailRms);
    REQUIRE(busBurstRms > kSyncReturnFloor);
    REQUIRE(busTailRms < busBurstRms * kSyncBusStopRatio);

    std::size_t nonFinite = 0;
    const std::vector<double> env = isolatedReturnEnvelope(bus, withSend, measureBegin, nonFinite);
    CHECK(nonFinite == std::size_t{0});
    REQUIRE(env.size() > kSyncMaxLagHops);

    double envPeak = 0.0;
    for (const double v : env) {
        envPeak = std::max(envPeak, v);
    }
    REQUIRE(envPeak > kSyncReturnFloor);  // non-vacuity: the send is audible

    double bestScore = 0.0;
    double zeroLagScore = 0.0;
    const std::size_t lagHops = dominantPeriodHops(env, bestScore, zeroLagScore);
    const auto measuredSamples = static_cast<double>(lagHops * kSyncEnvHop);

    INFO("measured period = " << measuredSamples << " samples, autocorrelation peak/zero-lag = "
                              << (zeroLagScore > 0.0 ? bestScore / zeroLagScore : 0.0));
    // The peak is a real periodicity, not the least-bad lag in a flat band.
    CHECK(bestScore > 0.0);
    CHECK(std::fabs(measuredSamples - expectedSamples)
          <= static_cast<double>(kSyncToleranceSamples));
}

}  // namespace

// =============================================================================
// SC-005 - THE SPECTRAL DELAY DECAYS AT ITS REGISTERED MAXIMUM
// =============================================================================
// NOT TAGGED [.perf], deliberately, however long the two decay arms take: they
// are CORRECTNESS gates. The defect they exist to catch - a per-bin loop gain at
// or above unity, whose recursion tanh(delayedMag * binFeedback)
// (spectral_delay.h:751-767) has a STABLE NON-ZERO FIXED POINT - is a send that
// never stops, on a global always-summed bus.
// =============================================================================
TEST_CASE("Spectral delay decays at registered max feedback", "[seraphis][effects]") {

    // =========================================================================
    // CLAUSE 1 - the derived bound. Arithmetic only, no render.
    // =========================================================================
    // calculateTiltedFeedback multiplies the PUSHED feedback by a tilt factor
    // spanning [0, 2] and clamps the PRODUCT to kMaxFeedback = 1.2f
    // (spectral_delay.h:603-614, :99) - NOT to the registered maximum. With
    // FR-016a's compensation the worst per-bin gain is
    // (fb / (1 + |tilt|)) * (1 + |tilt|) = fb = 0.95 < 1. Without it the bound is
    // 1.2 and clauses 2 and 3 are unpassable.
    SECTION("the compensated per-bin loop gain is below unity at every tilt") {
        for (const float tilt : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            CAPTURE(tilt);
            const float compensated =
                Seraphis::tiltCompensatedFeedback(Seraphis::kFxDelayFeedbackMax, tilt);

            float worst = 0.0f;
            for (std::size_t b = 0; b < kDecayBinCount; ++b) {
                const float normalizedBin =
                    static_cast<float>(b) / static_cast<float>(kDecayBinCount - 1u);
                const float tiltFactor = 1.0f + tilt * (normalizedBin - 0.5f) * 2.0f;
                worst = std::max(worst, std::clamp(compensated * tiltFactor, 0.0f,
                                                   Krate::DSP::SpectralDelay::kMaxFeedback));
            }
            INFO("worst per-bin loop gain over " << kDecayBinCount << " bins = " << worst);
            CHECK(worst < 1.0f);
        }

        // THE NEGATIVE CONTROL. Without the compensation the registration cap
        // bounds NOTHING: at tilt +1 every bin above normalizedBin = 0.5263 -
        // 243 of the 513 - receives a loop gain above unity and sustains forever.
        // If this ever stops holding, clause 1 above has stopped proving that
        // FR-016a is what establishes the bound.
        std::size_t binsAboveUnity = 0;
        for (std::size_t b = 0; b < kDecayBinCount; ++b) {
            const float normalizedBin =
                static_cast<float>(b) / static_cast<float>(kDecayBinCount - 1u);
            const float tiltFactor = 1.0f + 1.0f * (normalizedBin - 0.5f) * 2.0f;
            const float uncompensated =
                std::clamp(Seraphis::kFxDelayFeedbackMax * tiltFactor, 0.0f,
                           Krate::DSP::SpectralDelay::kMaxFeedback);
            if (uncompensated > 1.0f) {
                ++binsAboveUnity;
            }
        }
        INFO("uncompensated bins above unity loop gain at tilt +1 = " << binsAboveUnity);
        CHECK(binsAboveUnity == std::size_t{243});
    }

    // =========================================================================
    // CLAUSES 2 + 3 - TWO SEPARATE RUNS, one per tilt extreme
    // =========================================================================
    SECTION("the isolated return decays at tilt -1") {
        checkDecayArm(kDecayTiltLowNormalized, "tilt = -1 (full low bias)");
    }

    SECTION("the isolated return decays at tilt +1") {
        checkDecayArm(kDecayTiltHighNormalized, "tilt = +1 (full high bias)");
    }
}

// =============================================================================
// SC-019 - SYNCED DELAY TRACKS HOST TEMPO
// =============================================================================
TEST_CASE("Synced delay tracks host tempo", "[seraphis][effects]") {

    // =========================================================================
    // THE MEASUREMENT MACHINERY, CHECKED FIRST, ON SYNTHETIC DATA
    // =========================================================================
    // So a red run below reads as "the plugin's period is wrong" and never as
    // "the estimator cannot find a period". The probe is the same shape the real
    // arms present: a decaying train of identical blobs on a DC pedestal, which
    // is what an isolated return's envelope looks like.
    SECTION("the period estimator recovers a known comb") {
        constexpr std::size_t kProbePeriodHops = 125;  // 8000 samples at hop 64
        static_assert(kProbePeriodHops > kSyncMinLagHops && kProbePeriodHops < kSyncMaxLagHops,
                      "the probe period must sit inside the searched band");

        std::vector<double> env(2400, 0.02);  // a pedestal the mean removal must handle
        double amplitude = 1.0;
        for (std::size_t k = 1; k * kProbePeriodHops < env.size(); ++k) {
            const std::size_t centre = k * kProbePeriodHops;
            for (std::size_t j = 0; j < 8u; ++j) {
                if (centre + j < env.size()) {
                    env[centre + j] += amplitude * (1.0 - 0.125 * static_cast<double>(j));
                }
            }
            amplitude *= 0.7;
        }

        double bestScore = 0.0;
        double zeroLagScore = 0.0;
        const std::size_t lagHops = dominantPeriodHops(env, bestScore, zeroLagScore);
        INFO("probe: recovered " << lagHops * kSyncEnvHop << " samples, expected "
                                 << kProbePeriodHops * kSyncEnvHop);
        CHECK(bestScore > 0.0);
        CHECK(lagHops * kSyncEnvHop >= kProbePeriodHops * kSyncEnvHop - kSyncToleranceSamples);
        CHECK(lagHops * kSyncEnvHop <= kProbePeriodHops * kSyncEnvHop + kSyncToleranceSamples);
    }

    // =========================================================================
    // THE LABEL NAMES THE PERIOD (the runtime half of FR-017's pairing)
    // =========================================================================
    // dropdown_mappings.h:289 already ties index 7's LABEL to the period
    // dropdownToDelayMs produces at COMPILE time. This is the same statement for
    // the indices the arms below actually measure, so a permuted table cannot
    // survive by being permuted consistently with one static_assert.
    SECTION("the label table names the periods measured below") {
        CHECK(labelEquals(Seraphis::kFxDelaySyncNoteLabels[7], "1/16"));
        CHECK(labelEquals(Seraphis::kFxDelaySyncNoteLabels[9], "1/8T"));
        // 1/16 = 0.25 beats and 1/8T = 1/3 beat, i.e. exactly what the two
        // strings say, at the two tempi the arms use.
        CHECK(Krate::DSP::dropdownToDelayMs(7, 120.0) == Catch::Approx(125.0).margin(1.0e-3));
        CHECK(Krate::DSP::dropdownToDelayMs(7, 90.0)
              == Catch::Approx(0.25 * 60000.0 / 90.0).margin(1.0e-3));
        CHECK(Krate::DSP::dropdownToDelayMs(9, 140.0)
              == Catch::Approx((1.0 / 3.0) * 60000.0 / 140.0).margin(1.0e-2));
    }

    // =========================================================================
    // THE CRITERION - two indices, two tempi
    // =========================================================================
    SECTION("a synced send measures the tempo-derived period") {
        for (const SyncConfig& cfg : kSyncedConfigs) {
            checkSyncConfig(cfg);
        }
    }

    // =========================================================================
    // FR-030's THREE-PART GUARD - the clause the component's own check misses
    // =========================================================================
    // SpectralDelay falls back only on `tempo <= 0.0` (spectral_delay.h:325-327).
    // A host that leaves a STALE, POSITIVE tempo in the context with kTempoValid
    // clear would therefore sync the send to a tempo the transport no longer has,
    // and a build that simply passed a default-constructed BlockContext
    // (tempoBPM = 120.0) would satisfy every other criterion in this phase while
    // ignoring the host outright.
    SECTION("an invalid transport falls back to 120 BPM") {
        for (const SyncConfig& cfg : kSyncFallbackConfigs) {
            checkSyncConfig(cfg);
        }
    }
}

// =============================================================================
// T016 - SC-007 AND SC-011a: THE SPECTRAL FREEZE, AND A MIX EXCURSION THROUGH 0
// =============================================================================
// Both criteria are measured on SC-003's ISOLATED SEND RETURN, read at FR-041
// clause 6's tap (kBlock = 512 <= 2048, preOutputTapTruncatedForTest() asserted
// false on EVERY block by renderTapBlocks above).
//
// SC-007'S TWO ARMS USE TWO DIFFERENT DIFFERENCE DEFINITIONS, AND THE SPLIT IS
// NORMATIVE (tasks.md T016; spec.md SC-007).
//
//   (a) DIFFERENCES THE FREEZE, NOT THE MIX. kFxDelayMixId is held at its C-6
//       default 0 in BOTH renders and the isolated quantity is
//       render(1430 = on) - render(1430 = off). SC-003's mix-differenced
//       definition MUST NOT be used here: it would mutate the very parameter this
//       arm pins, rendering one side at mix 1 (return gain 1.0) and the other at
//       mix 0 (return gain forced to kFxFreezeMinReturnGain = 0.5 by FR-023a), so
//       a build whose forced engage worked ONLY when mix > 0 - the exact defect
//       FR-023a exists to prevent - would still show a non-zero difference and
//       pass "RMS > -60 dBFS".
//
//   (b) USES SC-003's DEFINITION, with ONE clause that is not negotiable: the
//       mix = 0 REFERENCE render must have the freeze OFF. Engaging 1430 in the
//       reference would force the send active THERE TOO (FR-023a), at gain 0.5
//       and with its own engage history, so the "difference" would be a
//       difference of two send states rather than the send itself. The reference
//       is therefore the bus alone - which is what SC-003's definition means by
//       "render(kFxDelayMixId = 0)" in the first place.
//
// WHY THE ORDERING CLAUSE IS SPELLED AS THE PRIMING WINDOW (plan D-5).
// processSpectralFrame captures on the first frame where `freezing && !wasFrozen_`
// (spectral_delay.h:677-688), reading the STFT's CURRENT analysis frame. From the
// C-6 defaults the send has been bypassed since prepare, so at the instant of a
// forced engage that frame is zeros or a stale drained tail - capturing it gives a
// SILENT frozen spectrum and arm (a)'s "> -60 dBFS" fails for an implementation
// that follows FR-023a literally. The processor therefore holds
// setFreezeEnabled(true) back until the send has consumed kFxFreezePrimeSamples
// = 2 x kDefaultFFTSize = 2048 LIVE samples (four hops = two analyses on wholly-
// live frames = 42.7 ms at 48 kHz). "Capture at ENGAGE time, not at toggle time"
// is therefore observable as an exact block count, asserted below in BOTH
// directions: deferred by exactly kFreezePrimeBlocks when the send was bypassed
// (arm a), and NOT deferred at all when the send was already primed (arm b).
//
// NO CHECKED-IN FLOAT GOLDEN: every number below is either a ratio between two
// windows of ONE render pair made by this binary, or a named spec constant.
// =============================================================================
namespace {

// -----------------------------------------------------------------------------
// The D-5 priming window, in 512-sample blocks
// -----------------------------------------------------------------------------
// updateEffectsBypassState() runs ONCE per process() call and BEFORE
// pushEffectsParams() (processor.cpp:946-948), and it zeroes
// fxLiveSamplesSinceEngage_ on the engage block itself. So the counter reads
// 0, 512, 1024, 1536, 2048 on the engage block and the four after it: the push
// lands on engage + kFreezePrimeBlocks and NOT ONE BLOCK EARLIER, which is
// exactly what the two CHECK_FALSEs below pin.
constexpr std::size_t kFreezePrimeBlocks =
    static_cast<std::size_t>(Seraphis::kFxFreezePrimeSamples) / kBlockSamples;
static_assert(kFreezePrimeBlocks == 4u,
              "plan D-5: 2 x kDefaultFFTSize = 2048 samples = four 512-sample blocks");

/// kFreezeCrossfadeTimeMs = 75 ms (spectral_delay.h:906) advanced ONCE PER FRAME
/// (:692-696) with the increment derived at :210-212, i.e. hop / (0.075 * fs) =
/// 512 / 3600 = 0.1422 per frame => 8 frames. One frame is one accumulator chunk
/// is one 512-sample block, so the crossfade is 8 blocks - NOT one hop.
constexpr std::size_t kFreezeCrossfadeBlocks = 8;

/// The send's own output latency: fftSize (1024) plus the accumulator's fixed
/// one-chunk pipeline delay (512), i.e. three blocks.
constexpr std::size_t kFreezePipelineBlocks =
    (Krate::DSP::SpectralDelay::kDefaultFFTSize + Seraphis::kFxSendChunkSamples) / kBlockSamples;
static_assert(kFreezePipelineBlocks == 3u, "(1024 + 512) / 512");

// -----------------------------------------------------------------------------
// FR-009a's window in samples, at this file's 48 kHz - so the geometry below is
// written against kFxSendDrainMs itself rather than against a transcription of it
// -----------------------------------------------------------------------------
constexpr std::size_t kSamplesPerMsAt48k = 48;
constexpr std::size_t kSendDrainWindowSamples =
    static_cast<std::size_t>(Seraphis::kFxSendDrainMs) * kSamplesPerMsAt48k;
static_assert(kSendDrainWindowSamples == 96000u, "kFxSendDrainMs = 2000 ms at 48 kHz");

// -----------------------------------------------------------------------------
// SC-007's script geometry
// -----------------------------------------------------------------------------
/// ~3 s of held chord before ID 1430 moves, and the 3 is LOAD-BEARING rather than
/// a settle: FR-008's reset condition (a) is "bypassed for LONGER than
/// kFxSendDrainMs", so an engage inside the first 2 s would have `fxResetDue_`
/// false whether or not FR-023a's `&& !freezeOn` suppression exists, and arm (a)'s
/// `resets == 0` clause would be vacuous. Past 2 s the two builds differ.
constexpr std::size_t kFreezeEngageBlock = 3u * kIndepBlocksPerSecond;
static_assert(kFreezeEngageBlock * kBlockSamples > kSendDrainWindowSamples,
              "SC-007 arm (a): the engage must come after a WHOLE kFxSendDrainMs of bypass, or "
              "FR-023a's suppression of FR-008's reset is not what holds the reset count at 0");

/// The criterion's "200 ms after engagement", rounded UP to the block grid:
/// 19 x 512 = 9728 samples = 202.7 ms.
constexpr std::size_t kFreezePostEngageBlocks = 19;
static_assert(kFreezePostEngageBlocks * kBlockSamples * 1000u >= std::size_t{200} * 48000u,
              "SC-007: the measurement point must be at least 200 ms after the engage");
/// AND it must sit past the whole engage pipeline, or the window would straddle a
/// crossfade that is still running: priming + capture frame + crossfade + the
/// send's own output latency = 4 + 1 + 8 + 3 = 16 blocks.
static_assert(kFreezePostEngageBlocks
                  > kFreezePrimeBlocks + 1u + kFreezeCrossfadeBlocks + kFreezePipelineBlocks,
              "SC-007: 200 ms must clear priming + capture + crossfade + pipeline");

constexpr std::size_t kFreezeWindowBlocks = 47;  // ~0.5 s
constexpr std::size_t kFreezeWindowABlock = kFreezeEngageBlock + kFreezePostEngageBlocks;
constexpr std::size_t kFreezeNoteOffBlock = kFreezeEngageBlock + kIndepBlocksPerSecond;
static_assert(kFreezeWindowABlock + kFreezeWindowBlocks <= kFreezeNoteOffBlock,
              "SC-007: window A is measured with the chord still held");
constexpr std::size_t kFreezeWindowBBlock = kFreezeNoteOffBlock + 5u * kIndepBlocksPerSecond;
constexpr std::size_t kFreezeTotalBlocks = kFreezeWindowBBlock + kFreezeWindowBlocks;

/// "never" for the freeze-engage block of an arm that does not engage it.
constexpr std::size_t kFreezeNeverBlock = ~std::size_t{0};

// -----------------------------------------------------------------------------
// The criterion's thresholds, as RATIOS - never as dB comparisons, which would
// need a guard against log(0) at exactly the point the control drives the return
// to zero.
// -----------------------------------------------------------------------------
constexpr double kMinusSixtyDbfs = 1.0e-3;  ///< 10^(-60/20), against full scale
constexpr double kOneDbRatio = 1.1220185;   ///< 10^( +1/20)
constexpr double kTwoDbRatio = 1.2589254;   ///< 10^( +2/20)
constexpr double kMinusThirtyDbRatio = 3.1622777e-2;
constexpr double kMinusTwelveDbRatio = 2.5118864e-1;
constexpr double kMinusEightDbRatio = 3.9810717e-1;
/// SC-007(b)'s centroid clause.
constexpr double kCentroidTolerance = 0.05;
/// Non-vacuity floor for every RMS the two cases reference something to.
constexpr double kFreezeReturnFloor = 1.0e-4;

// -----------------------------------------------------------------------------
// The centroid estimator
// -----------------------------------------------------------------------------
// A Hann-windowed 2048-point magnitude spectrum, accumulated over BOTH channels
// and over every whole frame in the measured window, then
// centroid = sum(f_k * |X_k|) / sum(|X_k|).
//
// BOTH CHANNELS ARE ACCUMULATED IN THE MAGNITUDE DOMAIN, never mono-summed in the
// time domain: the send decorrelates its two outputs by design (stereoWidth
// default 0.5, spectral_delay.h:808-814), so L + R would cancel exactly where the
// decorrelation is strongest and report the centroid of the residue.
constexpr std::size_t kCentroidFftSize = 2048;
constexpr std::size_t kCentroidHopSamples = 1024;
static_assert(kCentroidFftSize >= Krate::DSP::kMinFFTSize
                  && kCentroidFftSize <= Krate::DSP::kMaxFFTSize,
              "the estimator's transform size must be one FFT::prepare accepts");
static_assert(kFreezeWindowBlocks * kBlockSamples >= 4u * kCentroidFftSize,
              "a 0.5 s window must carry several whole frames");

/// One tap-capturing render: the WHOLE pre-output-stage stereo bus, plus the two
/// FR-041 counters as they stood at the end of it.
struct TapCapture {
    std::vector<float> left, right;
    std::size_t sendChunks = 0;
    std::size_t resets = 0;
};

/// What the FR-023a / D-5 ordering clauses observe. Every field is sampled AFTER
/// the named block has been rendered.
struct FreezeOrdering {
    bool frozenAtEngage = true;               ///< arm (a): MUST be false (deferred)
    bool frozenOneBlockShortOfPrimed = true;  ///< arm (a): MUST be false
    bool frozenWhenPrimed = false;            ///< MUST be true
    std::size_t chunksBeforeEngage = 1;       ///< arm (a): MUST be 0 (FR-007)
};

/// `script(b, fx)` places automation and notes; `observer(b, fx)` is called after
/// each block, with the fixture still live, so a counter or a component getter can
/// be sampled MID-RENDER rather than inferred from the audio afterwards.
template <typename Script, typename Observer>
[[nodiscard]] TapCapture renderTapCapture(std::size_t numBlocks, const Script& script,
                                          const Observer& observer) {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    TapCapture out;
    out.left.reserve(numBlocks * kBlockSamples);
    out.right.reserve(numBlocks * kBlockSamples);

    renderTapBlocks(fx, numBlocks, script,
                    [&out, &fx, &observer](std::size_t b, std::span<const float> l,
                                           std::span<const float> r) {
                        out.left.insert(out.left.end(), l.begin(), l.end());
                        out.right.insert(out.right.end(), r.begin(), r.end());
                        observer(b, fx);
                    });

    out.sendChunks = fx.proc->sendChunkCountForTest();
    out.resets = fx.proc->spectralDelayResetCountForTest();
    return out;
}

/// The isolated return's RMS over [beginBlock, +numBlocks), both channels.
[[nodiscard]] double isolatedRms(const TapCapture& withSend, const TapCapture& reference,
                                 std::size_t beginBlock, std::size_t numBlocks,
                                 std::size_t& nonFinite) {
    const std::size_t begin = beginBlock * kBlockSamples;
    const std::size_t end = std::min({begin + numBlocks * kBlockSamples, withSend.left.size(),
                                      reference.left.size()});
    if (begin >= end) {
        return 0.0;
    }
    double acc = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        const float dl = withSend.left[i] - reference.left[i];
        const float dr = withSend.right[i] - reference.right[i];
        if (isNonFiniteBits(dl) || isNonFiniteBits(dr)) {
            ++nonFinite;
        }
        acc += static_cast<double>(dl) * static_cast<double>(dl)
               + static_cast<double>(dr) * static_cast<double>(dr);
    }
    return std::sqrt(acc / static_cast<double>(2u * (end - begin)));
}

/// The isolated return's spectral centroid in Hz over the same span.
[[nodiscard]] double isolatedCentroidHz(const TapCapture& withSend, const TapCapture& reference,
                                        std::size_t beginBlock, std::size_t numBlocks) {
    const std::size_t begin = beginBlock * kBlockSamples;
    const std::size_t end = std::min({begin + numBlocks * kBlockSamples, withSend.left.size(),
                                      reference.left.size()});
    if (begin + kCentroidFftSize > end) {
        return 0.0;
    }

    Krate::DSP::FFT fft;
    fft.prepare(kCentroidFftSize);

    // Built in double and narrowed once: the window multiplies BOTH measured
    // spans, so it must be the identical sequence of floats for both.
    constexpr double kTwoPiD = 6.283185307179586;
    std::vector<float> window(kCentroidFftSize, 0.0f);
    for (std::size_t i = 0; i < kCentroidFftSize; ++i) {
        window[i] = static_cast<float>(
            0.5
            - 0.5
                  * std::cos(kTwoPiD * static_cast<double>(i)
                             / static_cast<double>(kCentroidFftSize)));
    }

    std::vector<float> frame(kCentroidFftSize, 0.0f);
    std::vector<Krate::DSP::Complex> spectrum(kCentroidFftSize / 2u + 1u);
    std::vector<double> magnitude(kCentroidFftSize / 2u + 1u, 0.0);

    for (std::size_t start = begin; start + kCentroidFftSize <= end;
         start += kCentroidHopSamples) {
        for (int channel = 0; channel < 2; ++channel) {
            const std::vector<float>& a = (channel == 0) ? withSend.left : withSend.right;
            const std::vector<float>& r = (channel == 0) ? reference.left : reference.right;
            for (std::size_t i = 0; i < kCentroidFftSize; ++i) {
                frame[i] = (a[start + i] - r[start + i]) * window[i];
            }
            fft.forward(frame.data(), spectrum.data());
            for (std::size_t k = 0; k < magnitude.size(); ++k) {
                magnitude[k] += static_cast<double>(spectrum[k].magnitude());
            }
        }
    }

    double numerator = 0.0;
    double denominator = 0.0;
    for (std::size_t k = 0; k < magnitude.size(); ++k) {
        const double hz =
            static_cast<double>(k) * kSampleRate / static_cast<double>(kCentroidFftSize);
        numerator += hz * magnitude[k];
        denominator += magnitude[k];
    }
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

/// SC-007's script. `mixNormalized` is the arm's baseline send level (0 for the
/// bus reference and for the whole of arm (a)); `freezeBlock` places the ID 1430
/// engage, or kFreezeNeverBlock for an arm that never engages it.
///
/// NOTHING ELSE IS DRIVEN. Arm (a)'s subject is the SHIPPED PATCH, so all 16
/// effects parameters stay at their C-6 defaults and every Phase 9 surface stays
/// where the plugin registers it.
void applyFreezeScript(std::size_t b, Fixture& fx, double mixNormalized,
                       std::size_t freezeBlock) {
    if (b == 0) {
        fx.setParam(Seraphis::kFxDelayMixId, mixNormalized);
        for (const Steinberg::int16 pitch : kEightNoteChord) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
        }
    }
    if (b == freezeBlock) {
        fx.setParam(Seraphis::kFxSpectralFreezeId, 1.0);
    }
    if (b == kFreezeNoteOffBlock) {
        for (const Steinberg::int16 pitch : kEightNoteChord) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, pitch, 0.0f, 0);
        }
    }
}

/// The ordering observer, shared by both arms: it records the freeze state around
/// the priming window and the send's chunk count just before the engage.
[[nodiscard]] auto makeFreezeOrderingObserver(FreezeOrdering& ordering) {
    return [&ordering](std::size_t b, Fixture& fx) {
        const Krate::DSP::SpectralDelay& sd = Probe::spectralDelay(*fx.proc);
        if (b == kFreezeEngageBlock - 1u) {
            ordering.chunksBeforeEngage = fx.proc->sendChunkCountForTest();
        } else if (b == kFreezeEngageBlock) {
            ordering.frozenAtEngage = sd.isFreezeEnabled();
        } else if (b == kFreezeEngageBlock + kFreezePrimeBlocks - 1u) {
            ordering.frozenOneBlockShortOfPrimed = sd.isFreezeEnabled();
        } else if (b == kFreezeEngageBlock + kFreezePrimeBlocks) {
            ordering.frozenWhenPrimed = sd.isFreezeEnabled();
        }
    };
}

/// The bus alone: mix 0, freeze off. BOTH arms difference against this render -
/// see the banner for why arm (b)'s reference may not carry the freeze.
[[nodiscard]] TapCapture renderFreezeBusReference() {
    return renderTapCapture(
        kFreezeTotalBlocks,
        [](std::size_t b, Fixture& fx) { applyFreezeScript(b, fx, 0.0, kFreezeNeverBlock); },
        [](std::size_t, Fixture&) {});
}

}  // namespace

// =============================================================================
// SC-007 - THE SPECTRAL FREEZE CAPTURES AND HOLDS
// =============================================================================
TEST_CASE("Spectral freeze holds the Aether tail", "[seraphis][effects]") {

    // =========================================================================
    // ARM (a) - FROM THE C-6 DEFAULTS. The freeze is the ONLY thing that differs
    // between the two renders, and kFxDelayMixId is 0 in both.
    // =========================================================================
    SECTION("engaging 1430 alone on the shipped patch holds audible output") {
        FreezeOrdering ordering;
        const TapCapture frozen = renderTapCapture(
            kFreezeTotalBlocks,
            [](std::size_t b, Fixture& fx) {
                applyFreezeScript(b, fx, 0.0, kFreezeEngageBlock);
            },
            makeFreezeOrderingObserver(ordering));
        const TapCapture bus = renderFreezeBusReference();

        // --- FR-007: at the C-6 defaults the send had NEVER run --------------
        // Without this the arm could pass on a build that ran the send from
        // prepare, which is the cost C-3 exists to avoid.
        CHECK(ordering.chunksBeforeEngage == std::size_t{0});
        CHECK(bus.sendChunks == std::size_t{0});
        CHECK(bus.resets == std::size_t{0});

        // --- FR-023a: 1430 alone FORCED the send active ----------------------
        CHECK(frozen.sendChunks > std::size_t{0});
        // ...and SUPPRESSED FR-008's reset. Without the suppression, reset()
        // clears wasFrozen_/freezeCrossfade_ (spectral_delay.h:276-277) and the
        // frozen spectrum buffers (:256-257) in the very block the capture has to
        // happen in.
        CHECK(frozen.resets == std::size_t{0});

        // --- Plan D-5: the capture is at ENGAGE + the priming window ----------
        // Deferred on the engage block and on the block one short of primed;
        // present the moment 2048 live samples have been consumed.
        CHECK_FALSE(ordering.frozenAtEngage);
        CHECK_FALSE(ordering.frozenOneBlockShortOfPrimed);
        CHECK(ordering.frozenWhenPrimed);

        std::size_t nonFinite = 0;
        const double rmsA =
            isolatedRms(frozen, bus, kFreezeWindowABlock, kFreezeWindowBlocks, nonFinite);
        const double rmsB =
            isolatedRms(frozen, bus, kFreezeWindowBBlock, kFreezeWindowBlocks, nonFinite);
        INFO("isolated return: 200 ms after engage = " << rmsA
                                                       << ", 5 s after note-off = " << rmsB);
        CHECK(nonFinite == std::size_t{0});

        // Non-vacuity: a silent capture would "hold" trivially - and a silent
        // capture is EXACTLY what an implementation without plan D-5's priming
        // produces here, because the STFT still holds prepare-time zeros at the
        // instant of the forced engage.
        REQUIRE(rmsA > kFreezeReturnFloor);

        // --- THE CRITERION ---------------------------------------------------
        CHECK(rmsB > kMinusSixtyDbfs);
        CHECK(rmsB <= rmsA * kOneDbRatio);
        CHECK(rmsB >= rmsA / kOneDbRatio);
    }

    // =========================================================================
    // ARM (b) - THE SEND ALREADY AT MIX 1.0 (SC-003's definition)
    // =========================================================================
    SECTION("a freeze engaged over a live send holds level and centroid") {
        const TapCapture bus = renderFreezeBusReference();

        FreezeOrdering ordering;
        const TapCapture frozen = renderTapCapture(
            kFreezeTotalBlocks,
            [](std::size_t b, Fixture& fx) {
                applyFreezeScript(b, fx, 1.0, kFreezeEngageBlock);
            },
            makeFreezeOrderingObserver(ordering));
        const TapCapture unfrozen = renderTapCapture(
            kFreezeTotalBlocks,
            [](std::size_t b, Fixture& fx) { applyFreezeScript(b, fx, 1.0, kFreezeNeverBlock); },
            [](std::size_t, Fixture&) {});

        REQUIRE(bus.sendChunks == std::size_t{0});
        REQUIRE(frozen.sendChunks > std::size_t{0});
        REQUIRE(unfrozen.sendChunks > std::size_t{0});

        // --- The OTHER half of the ordering clause ---------------------------
        // The send has been active since block 0 here, so it is primed long
        // before 1430 moves and the push is NOT deferred: the capture is at
        // ENGAGE, which on this arm IS the toggle block. A build that deferred
        // unconditionally (a fixed delay rather than a live-sample count) fails
        // this, and a build that pushed unconditionally fails arm (a).
        CHECK(ordering.chunksBeforeEngage > std::size_t{0});
        CHECK(ordering.frozenAtEngage);
        CHECK(ordering.frozenWhenPrimed);

        std::size_t nonFinite = 0;
        const double rmsA =
            isolatedRms(frozen, bus, kFreezeWindowABlock, kFreezeWindowBlocks, nonFinite);
        const double rmsB =
            isolatedRms(frozen, bus, kFreezeWindowBBlock, kFreezeWindowBlocks, nonFinite);
        const double offA =
            isolatedRms(unfrozen, bus, kFreezeWindowABlock, kFreezeWindowBlocks, nonFinite);
        const double offB =
            isolatedRms(unfrozen, bus, kFreezeWindowBBlock, kFreezeWindowBlocks, nonFinite);
        CHECK(nonFinite == std::size_t{0});

        const double centroidA =
            isolatedCentroidHz(frozen, bus, kFreezeWindowABlock, kFreezeWindowBlocks);
        const double centroidB =
            isolatedCentroidHz(frozen, bus, kFreezeWindowBBlock, kFreezeWindowBlocks);

        INFO("frozen RMS: A=" << rmsA << " B=" << rmsB << "  |  unfrozen RMS: A=" << offA
                              << " B=" << offB << "  |  centroid: A=" << centroidA << " Hz B="
                              << centroidB << " Hz");

        REQUIRE(rmsA > kFreezeReturnFloor);
        REQUIRE(offA > kFreezeReturnFloor);
        REQUIRE(centroidA > 0.0);

        // --- THE CRITERION ---------------------------------------------------
        CHECK(rmsB <= rmsA * kOneDbRatio);
        CHECK(rmsB >= rmsA / kOneDbRatio);
        CHECK(std::fabs(centroidB - centroidA) <= kCentroidTolerance * centroidA);

        // ...and the same measurement with the freeze OFF decays >= 30 dB, which
        // is what makes the two clauses above a statement about the FREEZE rather
        // than about a send that would have held on regardless.
        CHECK(offB <= offA * kMinusThirtyDbRatio);
    }
}

// =============================================================================
// T016 - SC-011a: A MIX EXCURSION THROUGH ZERO DOES NOT DESTROY THE TAIL
// =============================================================================
// THREE ARMS, AND THE SPLIT IS FORCED BY FR-023a.
//
//   A. freeze ON across a SHORT excursion. This is the criterion's literal
//      operating point ("...and a captured freeze"): the frozen return is
//      STATIONARY, which is the only thing that makes a +-2.0 dB comparison
//      across 1.5 s of render well-posed at all, and it is where the "frozen
//      spectrum's centroid within 5 %" clause lives.
//   B. freeze OFF across the SAME short excursion.
//   C. freeze OFF across an excursion LONGER than kFxSendDrainMs.
//
// B AND C ARE THE MANDATORY CONTROL PAIR, and they must both be freeze-OFF: with
// 1430 engaged, FR-023a's `wantActive = (mix != 0) || freezeOn` keeps the send
// ACTIVE through any mix excursion whatsoever, so a freeze-on "long excursion"
// would never drain, never reset, and the control would be vacuous - it would
// prove the freeze is the discriminator rather than the window. Holding the
// freeze fixed at OFF across B and C leaves the EXCURSION LENGTH as the only
// difference between them, which is the claim SC-011a actually makes.
//
// WHY THE CONTROL EXCURSION IS FIVE SECONDS AND NOT TWO. FR-008's reset needs
// fxBypassedSamples_ > kFxSendDrainMs, and fxBypassedSamples_ only starts
// accumulating once the DRAIN has ended (processor.cpp:1985-1994): the drain is a
// separate, whole kFxSendDrainMs window. The reset therefore needs a bypass span
// of TWO drain windows - 4 s - so 5 s is the shortest round figure on which the
// control's reset actually fires.
//
// WHY THE POST WINDOW STARTS 500 ms AFTER RE-ENGAGEMENT RATHER THAN AT IT. The
// send's recursion is y(t) = x(t-T) + 0.6*y(t-T) with T = 250 ms, so the 200 ms of
// silence the drain fed it comes back around as a HOLE at t-T: over
// [re-engage, +500 ms) a CORRECT implementation measures ~2.4 dB below its
// pre-excursion RMS (0.05 s at 2.5x, 0.2 s at 1.5x, 0.05 s at 2.5x, 0.2 s at
// 1.9x, against a steady state of 1/(1-0.6) = 2.5x), which would fail a +-2.0 dB
// clause outright. One further traversal later the deficit is ~0.75 dB. The
// window is therefore [re-engage + 500 ms, re-engage + 1000 ms) - "the RMS 500 ms
// after re-engagement" - and the hole itself is measured separately as the
// IMMEDIATE window, where it is the control pair's discriminator.
// =============================================================================
namespace {

constexpr float kExcFeedbackPlain = 0.6f;
constexpr double kExcFeedbackNormalized =
    static_cast<double>(kExcFeedbackPlain) / static_cast<double>(Seraphis::kFxDelayFeedbackMax);
constexpr float kExcDelayMsPlain = 250.0f;
constexpr double kExcDelayNormalized =
    static_cast<double>(kExcDelayMsPlain - Seraphis::kFxDelayTimeMinMs)
    / static_cast<double>(Seraphis::kFxDelayTimeMaxMs - Seraphis::kFxDelayTimeMinMs);

constexpr std::size_t kExcFreezeBlock = kIndepBlocksPerSecond;      // ~1 s in, arm A only
constexpr std::size_t kExcStartBlock = 2u * kIndepBlocksPerSecond;  // ~2 s in
constexpr std::size_t kExcWindowBlocks = 47;                        // ~0.5 s
constexpr std::size_t kExcImmediateBlocks = 23;                     // ~245 ms
constexpr std::size_t kExcPreWindowBlock = kExcStartBlock - kExcWindowBlocks;
static_assert(kExcImmediateBlocks < kExcWindowBlocks,
              "the immediate window must end before the recovery window begins");

/// 19 x 512 = 9728 samples = 202.7 ms, the criterion's pinned 200 ms on the block
/// grid.
constexpr std::size_t kExcShortBlocks = 19;
static_assert(kExcShortBlocks * kBlockSamples < kSendDrainWindowSamples,
              "SC-011a: the short excursion must be well inside kFxSendDrainMs");
/// 470 x 512 = 240 640 samples = 5.01 s.
constexpr std::size_t kExcLongBlocks = 470;
static_assert(kExcLongBlocks * kBlockSamples > 2u * kSendDrainWindowSamples,
              "SC-011a control: FR-008's reset needs a whole DRAIN window plus a whole "
              "BYPASSED window - the drain does not count toward fxBypassedSamples_");

constexpr std::size_t kExcShortReEngageBlock = kExcStartBlock + kExcShortBlocks;
constexpr std::size_t kExcLongReEngageBlock = kExcStartBlock + kExcLongBlocks;
constexpr std::size_t kExcShortTotalBlocks = kExcShortReEngageBlock + 2u * kExcWindowBlocks;
constexpr std::size_t kExcLongTotalBlocks = kExcLongReEngageBlock + 2u * kExcWindowBlocks;

/// SC-011a's script. The chord is HELD FOR THE WHOLE RENDER - there is no
/// note-off - because the +-2.0 dB clause compares two windows 1.5 s apart and a
/// releasing bus would put its own decay into that comparison.
///
/// kAtmosLevelId IS DRIVEN TO 0, on SC-005's precedent (applyDecayScript above):
/// the granular atmosphere is a STOCHASTIC sustaining layer (4 gr/s, 4 s grains)
/// and its wobble would land inside the tolerance the criterion spends on the
/// send. Everything else stays at its registered default.
void applyExcursionScript(std::size_t b, Fixture& fx, double mixNormalized,
                          std::size_t excursionBlocks, bool freeze) {
    if (b == 0) {
        fx.setParam(Seraphis::kFxDelayMixId, mixNormalized);
        fx.setParam(Seraphis::kFxDelayFeedbackId, kExcFeedbackNormalized);
        fx.setParam(Seraphis::kFxDelayTimeId, kExcDelayNormalized);
        fx.setParam(Seraphis::kAtmosLevelId, 0.0);
        for (const Steinberg::int16 pitch : kEightNoteChord) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
        }
    } else if (freeze && b == kExcFreezeBlock) {
        fx.setParam(Seraphis::kFxSpectralFreezeId, 1.0);
    } else if (b == kExcStartBlock) {
        // EXACTLY 0 - FR-007's predicate is an exact comparison, and an epsilon
        // here would leave the send engaged and the case measuring nothing.
        fx.setParam(Seraphis::kFxDelayMixId, 0.0);
    } else if (b == kExcStartBlock + excursionBlocks) {
        fx.setParam(Seraphis::kFxDelayMixId, mixNormalized);
    }
}

/// One excursion arm and the three windows it is judged on.
struct ExcursionArm {
    double pre = 0.0;        ///< the 0.5 s ending at the excursion
    double immediate = 0.0;  ///< the ~245 ms starting at re-engagement
    double recover = 0.0;    ///< the 0.5 s starting 0.5 s after re-engagement
    std::size_t resets = 0;
    std::size_t nonFinite = 0;
};

[[nodiscard]] ExcursionArm measureExcursion(const TapCapture& arm, const TapCapture& reference,
                                            std::size_t reEngageBlock) {
    ExcursionArm out;
    out.resets = arm.resets;
    out.pre = isolatedRms(arm, reference, kExcPreWindowBlock, kExcWindowBlocks, out.nonFinite);
    out.immediate = isolatedRms(arm, reference, reEngageBlock, kExcImmediateBlocks, out.nonFinite);
    out.recover = isolatedRms(arm, reference, reEngageBlock + kExcWindowBlocks, kExcWindowBlocks,
                              out.nonFinite);
    return out;
}

}  // namespace

TEST_CASE("A short mix excursion preserves the send tail", "[seraphis][effects]") {

    // THE BUS ALONE, rendered to the LONGEST arm's length and shared by all three
    // arms: the MIDI script and every non-mix parameter are identical across them,
    // and the send is the only thing that writes the bus after C-1 step 3, so one
    // reference is the correct reference for all of them.
    const TapCapture bus = renderTapCapture(
        kExcLongTotalBlocks,
        [](std::size_t b, Fixture& fx) {
            applyExcursionScript(b, fx, 0.0, kExcLongBlocks, /*freeze*/ false);
        },
        [](std::size_t, Fixture&) {});
    REQUIRE(bus.sendChunks == std::size_t{0});  // FR-007, at mix 0 throughout

    // =========================================================================
    // ARM A - THE CRITERION: a captured freeze across a 200 ms excursion
    // =========================================================================
    SECTION("a captured freeze survives a 200 ms excursion") {
        const TapCapture frozen = renderTapCapture(
            kExcShortTotalBlocks,
            [](std::size_t b, Fixture& fx) {
                applyExcursionScript(b, fx, 1.0, kExcShortBlocks, /*freeze*/ true);
            },
            [](std::size_t, Fixture&) {});

        const ExcursionArm a = measureExcursion(frozen, bus, kExcShortReEngageBlock);
        INFO("frozen arm: pre=" << a.pre << " immediate=" << a.immediate
                                << " recover=" << a.recover);
        CHECK(a.nonFinite == std::size_t{0});

        // FR-008 / FR-023a: nothing reset, so neither the tail nor the captured
        // spectrum was cleared.
        CHECK(a.resets == std::size_t{0});

        REQUIRE(a.pre > kFreezeReturnFloor);  // non-vacuity

        // --- THE CRITERION: +-2.0 dB, and the centroid within 5 % ------------
        CHECK(a.recover <= a.pre * kTwoDbRatio);
        CHECK(a.recover >= a.pre / kTwoDbRatio);

        const double centroidBefore =
            isolatedCentroidHz(frozen, bus, kExcPreWindowBlock, kExcWindowBlocks);
        const double centroidAfter = isolatedCentroidHz(
            frozen, bus, kExcShortReEngageBlock + kExcWindowBlocks, kExcWindowBlocks);
        INFO("frozen centroid: before=" << centroidBefore << " Hz after=" << centroidAfter
                                        << " Hz");
        REQUIRE(centroidBefore > 0.0);
        CHECK(std::fabs(centroidAfter - centroidBefore) <= kCentroidTolerance * centroidBefore);
    }

    // =========================================================================
    // ARMS B AND C - THE MANDATORY CONTROL PAIR. Identical in every respect
    // except the excursion length.
    // =========================================================================
    SECTION("the drain window, not the crossing, is what preserves the tail") {
        const TapCapture shortArm = renderTapCapture(
            kExcShortTotalBlocks,
            [](std::size_t b, Fixture& fx) {
                applyExcursionScript(b, fx, 1.0, kExcShortBlocks, /*freeze*/ false);
            },
            [](std::size_t, Fixture&) {});
        const TapCapture longArm = renderTapCapture(
            kExcLongTotalBlocks,
            [](std::size_t b, Fixture& fx) {
                applyExcursionScript(b, fx, 1.0, kExcLongBlocks, /*freeze*/ false);
            },
            [](std::size_t, Fixture&) {});

        const ExcursionArm s = measureExcursion(shortArm, bus, kExcShortReEngageBlock);
        const ExcursionArm l = measureExcursion(longArm, bus, kExcLongReEngageBlock);
        INFO("short: pre=" << s.pre << " immediate=" << s.immediate << " recover=" << s.recover
                           << " resets=" << s.resets << "  |  long: pre=" << l.pre
                           << " immediate=" << l.immediate << " recover=" << l.recover
                           << " resets=" << l.resets);
        CHECK(s.nonFinite == std::size_t{0});
        CHECK(l.nonFinite == std::size_t{0});

        REQUIRE(s.pre > kFreezeReturnFloor);
        REQUIRE(l.pre > kFreezeReturnFloor);

        // --- SHORT: FR-008's reset does NOT fire, and the tail survives -------
        CHECK(s.resets == std::size_t{0});
        // NO +-2.0 dB CLAUSE HERE, DELIBERATELY. SC-011a attaches that clause to
        // an excursion taken "with a captured freeze" (spec.md:1478-1484), and arm
        // A above is where it is asserted. This arm is UNFROZEN, so its return is
        // a decaying recirculation being re-fed by the live bus, not a stationary
        // captured spectrum: the drain window is fed silence and "the tail is
        // attenuated in proportion to the excursion length" (spec.md:583-584,
        // :875-876), then rebuilds over roughly one delay traversal per pass. It
        // is therefore still climbing when the recovery window opens - measured
        // -8.6 dB against pre at re-engage + 500 ms, from a -5.1 dB immediate -
        // and requiring +-2.0 dB of it would forbid exactly the FR-009a behaviour
        // the spec chose. What this pair must show is that the WINDOW is the
        // discriminator, which is the three clauses that remain.
        // The tail is THERE the instant the return comes back - the clause the
        // long arm below inverts, and the reason this pair is not vacuous.
        CHECK(s.immediate > s.pre * kMinusTwelveDbRatio);

        // --- LONG: the send was reset, the tail is gone, then it rebuilds -----
        CHECK(l.resets == std::size_t{1});
        CHECK(l.immediate <= l.pre * kMinusThirtyDbRatio);
        CHECK(l.recover > l.pre * kMinusEightDbRatio);
    }
}

// =============================================================================
// T018 - SC-006: THE TRUE-PEAK CEILING HOLDS WITH EVERY EFFECT ACTIVE
// =============================================================================
// THE MEASUREMENT IS A RAW OUTPUT SAMPLE PEAK, AND THAT IS A DECISION, NOT A
// SHORTCUT. SC-006 states it explicitly: the criterion is "deliberately NOT an
// independently-written 4x reconstruction". TruePeakLimiter bounds the signal at
// ITS OWN 4x oversampled resolution, through its internal polyphase upsampler
// (true_peak_limiter.h:38-42, :110-125). A second, differently-written test-side
// interpolator does not agree with that one to better than a fraction of a dB -
// its phase response, its taps and its group delay are all different - so a
// correct implementation would be reported as a failure by an amount that says
// nothing about the limiter. The raw sample peak against the ceiling plus
// kCeilingAllowanceDb is what every shipped Seraphis ceiling assertion measures
// (param_flow_test.cpp:59-63, processor_audio_test.cpp:148-153, applied at :810)
// and this criterion follows it.
//
// WHY THIS IS THE POST-LIMITER OUTPUT AND NOT THE TAP. Unlike SC-003(b)/(c),
// SC-005, SC-007, SC-011a and SC-019, the LIMITER IS THIS CRITERION'S SUBJECT.
// Reading at preOutputTapForTest() would measure C-1 step 5's output - i.e. the
// signal BEFORE the stage whose bound is being asserted - and the case would be
// vacuous. The tap is still read on every block, but only to REPORT how far into
// gain reduction the render drove the limiter, which is what tells a later reader
// whether the ceiling was actually exercised.
//
// "ALL 16 EFFECTS PARAMETERS AT MAXIMA" IS TAKEN LITERALLY: every one of C-6's
// sixteen registered IDs is driven to normalized 1.0 at block 0. That delivers
// the six the criterion names explicitly - delay mix 1.0, feedback
// kFxDelayFeedbackMax = 0.95, width kFxWidthMaxPercent = 200 %, wander depth 1.0,
// azimuth depth 1.0, saturation 1.0 - and, for the rows whose "maximum" is a
// toggle or a dropdown index rather than a magnitude, the top of the registered
// range: sync ON, sync note index 9, spread direction index 2 (CenterOut),
// spread kFxDelaySpreadMaxMs, tilt kFxDelayTiltMax, diffusion 1, delay width 1,
// wander rate 1, spectral freeze ON. Driving them from a single table rather than
// from sixteen hand-written lines is what makes "all sixteen" checkable: the
// static_assert below fails if the band ever grows and this case is not updated,
// instead of silently measuring fifteen of seventeen.
//
// NO CHECKED-IN FLOAT GOLDEN: the only pinned number is the limiter's own shipped
// ceiling, and the bound is DERIVED from it and from kCeilingAllowanceDb at
// runtime rather than transcribed.
// =============================================================================
namespace {

/// 2820 x 512 = 1 443 840 samples = 30.08 s at 48 kHz - the criterion's 30 s on
/// the block grid, rounded UP so the render is never short of what it claims.
constexpr std::size_t kMaximaRenderBlocks = 30u * kIndepBlocksPerSecond;
static_assert(kMaximaRenderBlocks * kBlockSamples >= std::size_t{30} * 48000u,
              "SC-006: the render must be at least 30 s at 48 kHz");

/// C-6's sixteen registered effects IDs, in table order. THE COUNT IS ASSERTED,
/// because "all 16" is part of the criterion rather than a detail of the script.
constexpr Steinberg::Vst::ParamID kEffectsParamIds[] = {
    Seraphis::kFxSaturationId,           Seraphis::kFxDelayMixId,
    Seraphis::kFxDelayTimeId,            Seraphis::kFxDelaySpreadId,
    Seraphis::kFxDelaySpreadDirectionId, Seraphis::kFxDelayFeedbackId,
    Seraphis::kFxDelayTiltId,            Seraphis::kFxDelayDiffusionId,
    Seraphis::kFxDelayWidthId,           Seraphis::kFxDelaySyncId,
    Seraphis::kFxDelaySyncNoteId,        Seraphis::kFxSpectralFreezeId,
    Seraphis::kFxWidthId,                Seraphis::kFxWanderDepthId,
    Seraphis::kFxWanderRateId,           Seraphis::kFxAzimuthDepthId};
constexpr std::size_t kEffectsParamIdCount =
    sizeof(kEffectsParamIds) / sizeof(kEffectsParamIds[0]);
static_assert(kEffectsParamIdCount == 16u,
              "SC-006 / C-6: the Phase 10 band is exactly sixteen IDs, and this case must "
              "drive EVERY one of them to its maximum");

/// The registered maximum of every row, normalized. Each of C-6's sixteen
/// denormalization rows is monotone non-decreasing in the normalized value
/// (effects_params.h handleEffectsParamChange), so 1.0 IS the maximum for all of
/// them - magnitudes, toggles and dropdown indices alike.
constexpr double kMaximaNormalized = 1.0;

/// The six the criterion names, DERIVED from the shipped ranges so the banner
/// above cannot drift away from what the script actually drives.
static_assert(static_cast<float>(kMaximaNormalized * Seraphis::kFxDelayFeedbackMax)
                  == Seraphis::kFxDelayFeedbackMax,
              "SC-006: normalized 1.0 on ID 1414 is the registered feedback maximum (0.95)");
static_assert(static_cast<float>(Seraphis::kFxWidthMinPercent
                                 + kMaximaNormalized
                                       * (Seraphis::kFxWidthMaxPercent
                                          - Seraphis::kFxWidthMinPercent))
                  == Seraphis::kFxWidthMaxPercent,
              "SC-006: normalized 1.0 on ID 1440 is the registered width maximum (200 %)");

/// "master gain at maximum": kMasterGainId is `normalized * 2.0`
/// (global_params.h:91-95), so 1.0 is linear 2.0 - the top of the registered
/// range, and the setting processor_audio_test.cpp:146 calls kMasterGainNormMax.
constexpr double kMaximaGainNormalized = 1.0;

/// "8 voices held". The eight-note chord is played at the HOST velocity maximum,
/// and polyphony is pushed EXPLICITLY at 7/15 -> 8 (global_params.h:97-101)
/// rather than left to the registered default, so a future default change fails
/// here instead of quietly turning this into a four-voice render.
constexpr double kMaximaPolyphonyNormalized = 7.0 / 15.0;
constexpr float kMaximaVelocity = 1.0f;

/// Non-vacuity floor on the OUTPUT peak. A silent render satisfies any ceiling,
/// so the case has to prove it rendered something first. Four orders below the
/// ceiling itself, so it can only fail if the render is essentially dead.
constexpr float kMaximaAudibleFloor = 0.1f;

/// Everything at block 0, so the operating point is in force before a single
/// sample is rendered and the whole 30 s is measured at the maxima.
void applyMaximaScript(std::size_t b, Fixture& fx) {
    if (b != 0) {
        return;
    }
    fx.setParam(Seraphis::kMasterGainId, kMaximaGainNormalized);
    fx.setParam(Seraphis::kPolyphonyId, kMaximaPolyphonyNormalized);
    for (const Steinberg::Vst::ParamID id : kEffectsParamIds) {
        fx.setParam(id, kMaximaNormalized);
    }
    for (const Steinberg::int16 pitch : kEightNoteChord) {
        fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kMaximaVelocity, 0);
    }
}

}  // namespace

TEST_CASE("Effects at maxima respect the true-peak ceiling", "[seraphis][effects]") {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    float worstOutputPeak = 0.0f;
    float worstTapPeak = 0.0f;
    std::size_t nonFinite = 0;

    for (std::size_t b = 0; b < kMaximaRenderBlocks; ++b) {
        applyMaximaScript(b, fx);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        // The tap is REPORTED, never asserted on, by this criterion - see the
        // banner. kBlock is 512 <= kMaxTapSamples, so it is never truncated, and
        // that precondition is checked rather than assumed.
        const std::span<const float> tapL = fx.proc->preOutputTapLForTest();
        const std::span<const float> tapR = fx.proc->preOutputTapRForTest();
        REQUIRE(tapL.size() == kBlockSamples);
        REQUIRE(tapR.size() == kBlockSamples);
        REQUIRE_FALSE(fx.proc->preOutputTapTruncatedForTest());
        for (std::size_t i = 0; i < kBlockSamples; ++i) {
            worstTapPeak = std::max({worstTapPeak, std::fabs(tapL[i]), std::fabs(tapR[i])});
        }

        // THE MEASURED QUANTITY: the raw plugin output.
        const float* outL = fx.audioL();
        const float* outR = fx.audioR();
        for (std::size_t i = 0; i < kBlockSamples; ++i) {
            if (isNonFiniteBits(outL[i]) || isNonFiniteBits(outR[i])) {
                ++nonFinite;
            }
            worstOutputPeak =
                std::max({worstOutputPeak, std::fabs(outL[i]), std::fabs(outR[i])});
        }
    }
    REQUIRE(fx.checkCanaries());

    // The send genuinely ran: at mix 1.0 FR-007's prohibition does not apply and
    // sendChunkCountForTest() counts one increment per spectralDelay_.process()
    // call (FR-041, plan D-8 clause 2). Without this the case could pass with the
    // whole Phase 10 stage inert.
    CHECK(fx.proc->sendChunkCountForTest() > std::size_t{0});

    const float ceiling = kLimiterCeilingLin * std::pow(10.0f, kCeilingAllowanceDb / 20.0f);

    INFO("SC-006: output peak " << worstOutputPeak << " vs ceiling " << ceiling << " ("
                                << kLimiterCeilingLin << " + " << kCeilingAllowanceDb
                                << " dB); pre-output-stage tap peak " << worstTapPeak
                                << "; non-finite output samples " << nonFinite);

    // Non-vacuity, in both directions.
    CHECK(worstOutputPeak >= kMaximaAudibleFloor);
    if (worstTapPeak <= kLimiterCeilingLin) {
        WARN("SC-006 (non-gating): the pre-limiter peak "
             << worstTapPeak << " never exceeded the ceiling " << kLimiterCeilingLin
             << ", so this render did not drive the limiter into gain reduction and the "
                "bound below was not exercised by a signal that needed limiting");
    }

    CHECK(nonFinite == std::size_t{0});

    // --- THE CRITERION -------------------------------------------------------
    CHECK(worstOutputPeak <= ceiling);
}

// =============================================================================
// T018 - SC-010: THE RENDER IS A PURE FUNCTION OF THE SEED
// =============================================================================
// TWO INDEPENDENTLY HEAP-ALLOCATED Processor INSTANCES, AND THAT IS THE WHOLE
// POINT. The defect FR-027 closes is SpectralDelay's own constructor seed,
// `reinterpret_cast<uintptr_t>(this) ^ sampleRate` (spectral_delay.h:223-225),
// which reset() then re-draws 2 x numBins stereo phases from (:279-284). On ONE
// instance `this` is a constant, so a build that never called seedRng() would
// reproduce itself perfectly and a same-instance re-render would pass. Two
// fixtures put the two SpectralDelay members at different addresses (Fixture::
// proc is a std::unique_ptr, seraphis_test_fixture.h:164), so this case is
// simultaneously the criterion AND its own negative control.
//
// THE RENDER CONFIGURATION IS PINNED, AND THE LIVE WANDER IS LOAD-BEARING.
// SC-010 is this phase's ONLY coverage of FR-026 (the two salted BrownianDrift
// seeds). At the C-6 defaults kFxWanderDepthId and kFxAzimuthDepthId are both 0,
// FR-010's mandatory skip removes C-1 step 5 from the signal path entirely, and
// neither drift reaches the output at all - so a build that never called
// widthDrift_.setSeed / azimuthDrift_.setSeed would render identically on both
// instances and pass. The pinned configuration therefore carries ALL FIVE of:
// the send active, the freeze exercised, kFxWidthId off 100 %, wander depth > 0
// and azimuth depth > 0 - and the case ASSERTS that the wander stage actually ran
// rather than assuming the depths were enough.
//
// TOLERANCES ARE render_fingerprint.h's MEASURED ONES (kSampleTolerance = 1e-4f,
// kMetricTolerance = 1e-5, :48-52). NO BIT-EXACT FLOAT GOLDEN: nothing is
// compared against a checked-in number - every comparison is between two renders
// this binary made in this process, and the two DISCRIMINATION clauses are stated
// as a multiple of the same measured tolerance rather than as an absolute.
//
// NO SECTIONs, DELIBERATELY. Catch2 re-runs the code outside a SECTION once per
// SECTION, so three sections would re-render the two baseline arms three times
// each - eight 20 s renders instead of four. All three clauses are asserted
// flat, off four renders.
// =============================================================================
namespace {

/// 1880 x 512 = 962 560 samples = 20.05 s at 48 kHz.
constexpr std::size_t kDetRenderBlocks = 20u * kIndepBlocksPerSecond;
static_assert(kDetRenderBlocks * kBlockSamples >= std::size_t{20} * 48000u,
              "SC-010: the render must be at least 20 s at 48 kHz");

// --- the PINNED configuration, every value derived from the shipped ranges ----
constexpr double kDetMixNormalized = 1.0;  ///< the send ACTIVE (a plain unit row)
constexpr float kDetFeedbackPlain = 0.6f;
constexpr double kDetFeedbackNormalized =
    static_cast<double>(kDetFeedbackPlain) / static_cast<double>(Seraphis::kFxDelayFeedbackMax);
constexpr float kDetDelayMsPlain = 250.0f;
constexpr double kDetDelayNormalized =
    static_cast<double>(kDetDelayMsPlain - Seraphis::kFxDelayTimeMinMs)
    / static_cast<double>(Seraphis::kFxDelayTimeMaxMs - Seraphis::kFxDelayTimeMinMs);

/// kFxWidthId OFF 100 %, which the criterion requires by name: normalized 0.5 is
/// exactly kDefaultWidth, so 1.0 (= kFxWidthMaxPercent = 200 %) is off it by the
/// whole upper half of the range. Asserted, not asserted-by-comment.
constexpr double kDetWidthNormalized = 1.0;
static_assert(static_cast<float>(Seraphis::kFxWidthMinPercent
                                 + kDetWidthNormalized
                                       * (Seraphis::kFxWidthMaxPercent
                                          - Seraphis::kFxWidthMinPercent))
                  != Krate::DSP::MidSideProcessor::kDefaultWidth,
              "SC-010: the pinned width must be OFF 100 %, or the M/S stage is a no-op and "
              "the width drift cannot reach the output");

/// Both strictly positive, which is what keeps FR-010's mandatory skip from
/// deleting C-1 step 5 - and therefore both BrownianDrifts - from the render.
/// Deliberately UNEQUAL, so a build that fed one drift's value to both targets
/// is not accidentally indistinguishable from the shipped one.
constexpr double kDetWanderDepthNormalized = 0.7;
constexpr double kDetAzimuthDepthNormalized = 0.4;
static_assert(kDetWanderDepthNormalized > 0.0 && kDetAzimuthDepthNormalized > 0.0,
              "SC-010: FR-026's coverage requires a LIVE wander - at depth 0 FR-010 skips "
              "the whole stage and both drifts are unobservable");
static_assert(kDetWanderDepthNormalized != kDetAzimuthDepthNormalized,
              "SC-010: the two depths differ so the two drift paths stay distinguishable");

// --- the script's timeline, in blocks ----------------------------------------
/// The chord is held for ~11 s and released with ~9 s of tail and freeze left in
/// the render, so both the sustained and the released halves are fingerprinted.
constexpr std::size_t kDetNoteOffBlock = 11u * kIndepBlocksPerSecond;
/// "freeze exercised" - engaged at ~8 s (well past D-5's 4-block priming window
/// and the send's 1024 + 512 pipeline) and released again at ~14 s, so BOTH
/// freeze transitions and both regimes are inside the compared render.
constexpr std::size_t kDetFreezeOnBlock = 8u * kIndepBlocksPerSecond;
constexpr std::size_t kDetFreezeOffBlock = 14u * kIndepBlocksPerSecond;
static_assert(kDetFreezeOnBlock < kDetNoteOffBlock && kDetNoteOffBlock < kDetFreezeOffBlock
                  && kDetFreezeOffBlock < kDetRenderBlocks,
              "SC-010: the freeze must engage while the chord is held and release before "
              "the render ends");

/// The two kSeedId indices clause (i) discriminates. handleGlobalParamChange
/// denormalizes kSeedId as clamp(int(v*15 + 0.5), 0, 15) (global_params.h:111-113),
/// so index/15 selects the index exactly - the same conversion
/// param_character_test.cpp:555-558 uses.
constexpr int kDetSeedIndexA = 3;
constexpr int kDetSeedIndexB = 9;
static_assert(kDetSeedIndexA != kDetSeedIndexB, "SC-010 clause (i) needs two DIFFERENT indices");
static_assert(Seraphis::kSeedValues[static_cast<std::size_t>(kDetSeedIndexA)]
                  != Seraphis::kSeedValues[static_cast<std::size_t>(kDetSeedIndexB)],
              "SC-010 clause (i): two indices into a table of DUPLICATE constants would make "
              "the discrimination unsatisfiable for a correct implementation");
/// NEITHER is 0. Index 0 is the registered default, so lastPushedFxSeedIndex_ is
/// already 0 after setupProcessing (processor.cpp:635) and a script driving it to
/// 0 would fire no seed burst at all (:1621) - the arms would then differ in
/// whether the burst ran, not in the seed it carried.
static_assert(kDetSeedIndexA != 0 && kDetSeedIndexB != 0,
              "SC-010: both indices must MOVE kSeedId, so both arms take the same code path");

[[nodiscard]] constexpr double detSeedNormalized(int index) noexcept {
    return static_cast<double>(index) / 15.0;
}

/// Clause (i) and (ii)'s bound: 100 x kMetricTolerance = 1e-3, i.e. two orders
/// above the measured cross-toolchain aggregate spread the tolerance encodes and
/// four above the 1.9e-7 that spread actually measured
/// (render_fingerprint.h:20-30).
constexpr double kDetDiscriminationBound = 100.0 * Krate::DSP::TestUtils::kMetricTolerance;
static_assert(kDetDiscriminationBound > Krate::DSP::TestUtils::kMetricTolerance,
              "a discrimination bound at or below the agreement tolerance would be "
              "satisfiable by two IDENTICAL renders");

/// One whole determinism render, plus the two facts the criterion's preconditions
/// are stated in terms of.
struct DetRender {
    std::vector<float> left, right;
    std::size_t sendChunks = 0;  ///< FR-007's counter: > 0 proves the send ran
    bool wanderRan = false;      ///< FR-010's RAW predicate, true on any block
};

/// `seedIndex` drives kSeedId; `swapSalts` runs SC-010 clause (ii)'s control.
///
/// THE SWAP IS APPLIED AFTER BLOCK 0 HAS BEEN RENDERED, and the ordering is
/// load-bearing: the script writes kSeedId on block 0, so the seed burst
/// (processor.cpp:1621-1638) fires DURING that block's process() call and
/// re-seeds both drifts with the shipped salts. A swap applied before it would be
/// overwritten and the control would silently measure nothing. After the burst
/// lastPushedFxSeedIndex_ has settled and the script never moves kSeedId again,
/// so nothing re-seeds the drifts for the remaining ~20 s.
[[nodiscard]] DetRender renderDeterminism(int seedIndex, bool swapSalts) {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    DetRender out;
    out.left.reserve(kDetRenderBlocks * kBlockSamples);
    out.right.reserve(kDetRenderBlocks * kBlockSamples);

    for (std::size_t b = 0; b < kDetRenderBlocks; ++b) {
        if (b == 0) {
            fx.setParam(Seraphis::kSeedId, detSeedNormalized(seedIndex));
            fx.setParam(Seraphis::kFxDelayMixId, kDetMixNormalized);
            fx.setParam(Seraphis::kFxDelayFeedbackId, kDetFeedbackNormalized);
            fx.setParam(Seraphis::kFxDelayTimeId, kDetDelayNormalized);
            fx.setParam(Seraphis::kFxWidthId, kDetWidthNormalized);
            fx.setParam(Seraphis::kFxWanderDepthId, kDetWanderDepthNormalized);
            fx.setParam(Seraphis::kFxAzimuthDepthId, kDetAzimuthDepthNormalized);
            for (const Steinberg::int16 pitch : kEightNoteChord) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
            }
        } else if (b == kDetFreezeOnBlock) {
            fx.setParam(Seraphis::kFxSpectralFreezeId, 1.0);
        } else if (b == kDetNoteOffBlock) {
            for (const Steinberg::int16 pitch : kEightNoteChord) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, pitch, 0.0f, 0);
            }
        } else if (b == kDetFreezeOffBlock) {
            fx.setParam(Seraphis::kFxSpectralFreezeId, 0.0);
        }

        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        if (b == 0 && swapSalts) {
            Probe::swapDriftSalts(
                *fx.proc, Seraphis::kSeedValues[static_cast<std::size_t>(seedIndex)]);
        }

        out.wanderRan = out.wanderRan || Probe::wanderRunsRaw(*fx.proc);

        const float* l = fx.audioL();
        const float* r = fx.audioR();
        out.left.insert(out.left.end(), l, l + kBlockSamples);
        out.right.insert(out.right.end(), r, r + kBlockSamples);
    }
    REQUIRE(fx.checkCanaries());

    out.sendChunks = fx.proc->sendChunkCountForTest();
    return out;
}

/// The worst relative aggregate-metric error between two renders, over BOTH
/// channels. This is the statistic SC-010's two discrimination clauses are stated
/// in ("a relative aggregate-metric difference > 100 x kMetricTolerance"), and
/// the same one compareFingerprints() gates the agreement clause on.
[[nodiscard]] Krate::DSP::TestUtils::FingerprintComparison detCompare(
    const std::vector<float>& actual, const std::vector<float>& reference) {
    return Krate::DSP::TestUtils::compareFingerprints(
        Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(actual)),
        Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(reference)));
}

}  // namespace

TEST_CASE("Effects renders are seed-deterministic", "[seraphis][effects]") {
    // Two INDEPENDENTLY heap-allocated instances of the same seeded sequence.
    // renderDeterminism() scopes its fixture, so only one engine (and its ~33 MB
    // of per-voice capture rings) is alive at a time even though four renders are
    // made here.
    const DetRender first = renderDeterminism(kDetSeedIndexA, /*swapSalts*/ false);
    const DetRender second = renderDeterminism(kDetSeedIndexA, /*swapSalts*/ false);

    // --- the pinned configuration's own preconditions -------------------------
    // Neither is decoration: without the first the whole case could pass with the
    // send inert, and without the second it could pass with FR-010 having skipped
    // C-1 step 5 - which is exactly the hole the wander clause exists to close.
    REQUIRE(first.sendChunks > std::size_t{0});
    REQUIRE(second.sendChunks == first.sendChunks);
    REQUIRE(first.wanderRan);
    REQUIRE(second.wanderRan);

    const auto agreeL = detCompare(second.left, first.left);
    const auto agreeR = detCompare(second.right, first.right);

    INFO("SC-010 agreement: L metric=" << agreeL.worstMetricRelativeError
                                       << " sample=" << agreeL.worstSampleError << " ("
                                       << agreeL.detail << "); R metric="
                                       << agreeR.worstMetricRelativeError
                                       << " sample=" << agreeR.worstSampleError << " ("
                                       << agreeR.detail << ")");

    // --- THE CRITERION: same seed, two instances, one process -----------------
    CHECK(agreeL.withinTolerance());
    CHECK(agreeR.withinTolerance());

    // --- CLAUSE (i): a different kSeedId index renders DIFFERENTLY ------------
    // This is what proves the seed reaches SpectralDelay::seedRng and both drifts
    // (FR-026, FR-027) rather than merely being stored.
    {
        const DetRender other = renderDeterminism(kDetSeedIndexB, /*swapSalts*/ false);
        REQUIRE(other.wanderRan);

        const double worst = std::max(detCompare(other.left, first.left).worstMetricRelativeError,
                                      detCompare(other.right, first.right).worstMetricRelativeError);
        INFO("SC-010 (i): seed index " << kDetSeedIndexA << " vs " << kDetSeedIndexB
                                       << " worst relative aggregate-metric difference " << worst
                                       << " (bound > " << kDetDiscriminationBound << ")");
        CHECK(worst > kDetDiscriminationBound);
    }

    // --- CLAUSE (ii): THE SALT-SWAP CONTROL ----------------------------------
    // The seed index is held FIXED at kDetSeedIndexA and the only difference is
    // which salt each drift's seed is XORed with. C-5 / FR-024a clause 3 forbids
    // the two drifts sharing one stream; without this clause that prohibition is
    // unobservable, because a build with identical salts still renders
    // deterministically and still discriminates two seed indices - it just makes
    // width and azimuth walk in lockstep, which no other assertion in the phase
    // can see.
    {
        const DetRender swapped = renderDeterminism(kDetSeedIndexA, /*swapSalts*/ true);
        REQUIRE(swapped.wanderRan);

        const double worst =
            std::max(detCompare(swapped.left, first.left).worstMetricRelativeError,
                     detCompare(swapped.right, first.right).worstMetricRelativeError);
        INFO("SC-010 (ii): salt-swap worst relative aggregate-metric difference "
             << worst << " (bound > " << kDetDiscriminationBound << ")");
        CHECK(worst > kDetDiscriminationBound);
    }
}

// =============================================================================
// T019 - SC-008: EVERY PHASE 10 TRANSITION IS CLICK-FREE
// =============================================================================
// Written in the shape of Phase 9's SC-005 (param_continuity_test.cpp:600-790),
// which is the criterion this one extends from "every automated parameter" to
// "every Phase 10 STATE transition". Its three load-bearing parts are carried
// over verbatim: the window is POSITIONED (not centred on the raw write), the
// reference is drawn from the SAME render at the SAME length with the SAME
// number of draws, and the finiteness clause is an INTEGER test on the bit
// pattern.
//
// THE SIX TRANSITIONS (spec SC-008, FR-010a): send-engage, freeze-on,
// freeze-off, wander-bypass engage, wander-bypass disengage, send-bypass.
//
// THE WINDOW IS PINNED BY THE SPEC AND IS NOT A FREE PARAMETER:
//
//     centre = event + AetherReverb::getLatencySamples() (1024)
//                    + kFxSendChunkSamples               (512)
//                    + SpectralDelay::kDefaultFFTSize    (1024)   = event + 2560
//
// i.e. +/- 10 ms POSITIONED IN THE OUTPUT DOMAIN. Phase 10 stacks the
// accumulator's fixed one-chunk pipeline delay and the send's FFT latency on top
// of the reverb's, so an unshifted window sits ~53 ms away from the audio the
// transition actually produced and measures the wrong programme material
// entirely - the same defect Phase 9 recorded for the reverb alone
// (specs/seraphis-phase9-parameters/spec.md:2100-2104). The shift is DERIVED
// here from the three shipped constants and cross-checked against the plugin's
// own reported latency at runtime, never transcribed as 2560.
//
// WHY THE SEND-BYPASS IS SCHEDULED EXACTLY kClickWindowShiftSamples AFTER THE
// WANDER DISENGAGE - and this is the one non-obvious thing in the layout, so it
// is written down rather than left to be rediscovered:
//
//   The 20 ms return-gain ramp of FR-008/FR-009 is a gain on the send's RETURN,
//   applied to the FIFO's output in the same slice the host's write lands in
//   (processor.cpp:2033-2039). Its edge is therefore at the WRITE sample, while
//   the window this criterion mandates sits 2560 samples LATER. Positive control
//   (b) - the mandatory one that snaps that ramp to instant - can only fail
//   clause 3 if a return-gain edge falls inside a MEASURED window at all, and
//   with a freely-spaced schedule not one of the six does. Placing the
//   send-bypass one shift after the wander disengage puts the bypass edge at the
//   exact CENTRE of the disengage's window, so the same geometry the spec pins
//   covers both marks. Note what this does NOT do: it does not move the window,
//   widen it, or relax the 1.5x bound - it makes the SHIPPED render strictly
//   harder to pass, because that window now has to be click-free across a real
//   return-gain ramp AND across FR-010a's latched settle drop (which lands at
//   +2880, also inside it).
//
// THE MEASURED SIGNAL IS THE PRE-OUTPUT-STAGE TAP, not the plugin output, for
// the reason T017's FR-010a section already states at :1998: the TruePeakLimiter
// chases a step, so measuring after it lets a real click be reported as the
// limiter's own gain reduction. kBlock is 512 <= kMaxBlockSamples, so the tap is
// never truncated - asserted per block by captureTapRender(), not assumed.
//
// NO SECTIONs, for the reason T018's banner gives: Catch2 re-runs everything
// outside a SECTION once per SECTION, which would re-render the 13 s baseline
// twice more. All four clauses and both positive controls are asserted flat.
// =============================================================================
namespace {

/// The three terms of the spec's shift, each from the constant that owns it.
/// AetherReverb::getLatencySamples() is `spectralEnabled_ ? diffusionFftSize_ : 0`
/// (aether_reverb.h:2607-2613) and makeSeraphisReverbConfig pins both, so the
/// plugin reports a CONSTANT 1024 (processor.cpp:1258-1270) - which the render
/// helper REQUIREs against this value rather than trusting the comment.
constexpr std::size_t kClickReverbLatency = 1024;
constexpr std::size_t kClickWindowShiftSamples = kClickReverbLatency
                                                + Seraphis::kFxSendChunkSamples
                                                + Krate::DSP::SpectralDelay::kDefaultFFTSize;
static_assert(kClickWindowShiftSamples == 2560u,
              "SC-008 clause 1: 1024 (reverb) + 512 (accumulator) + 1024 (fftSize)");

/// +/- 10 ms at 48 kHz, i.e. a 20 ms window - the same geometry Phase 9's SC-005
/// uses (param_continuity_test.cpp:613-615).
constexpr std::size_t kClickHalfWindow = 480;
constexpr std::size_t kClickWindowSamples = 2u * kClickHalfWindow;
/// Spelled as INTEGER arithmetic (x 100 = one second) rather than as
/// `0.010 * kSampleRate`: 0.01 is not representable in binary floating point and
/// a static_assert on the product is a coin flip on the rounding.
static_assert(kClickHalfWindow * 100u == static_cast<std::size_t>(kSampleRate),
              "SC-008 clause 1: the half-window IS 10 ms at this sample rate");
/// Clause 2's clearance: 50 ms.
constexpr std::size_t kClickFiftyMs = 2400;
static_assert(kClickFiftyMs * 20u == static_cast<std::size_t>(kSampleRate),
              "SC-008 clause 2: the reference clearance IS 50 ms at this sample rate");

/// Clause 3, and the SAME number T017's FR-010a section already uses for the
/// same statistic - one bound, not two transcriptions of one bound.
constexpr float kClickBoundFactor = kDisengageDeltaBound;
static_assert(kClickBoundFactor == 1.5f, "SC-008 clause 3: max(test) <= 1.5 x max(reference)");

// --- the layout, on a 1 s grid ----------------------------------------------
/// 94 x 512 = 48 128 >= 48 000, so every span below is at least the duration it
/// is named for. (The same rounding every other case in this TU uses.)
constexpr std::size_t kClickSecondBlocks = kIndepBlocksPerSecond;   // ~1 s
constexpr std::size_t kClickStepBlocks = 2u * kClickSecondBlocks;   // ~2 s
/// The shift, in whole blocks. The schedule is expressed in blocks, so the
/// pairing below is only exact because the shift divides the block length.
constexpr std::size_t kClickShiftBlocks = kClickWindowShiftSamples / kBlockSamples;  // 5
static_assert(kClickWindowShiftSamples % kBlockSamples == 0u,
              "the schedule places transitions on block boundaries, so the shift must be "
              "a whole number of blocks for the bypass/disengage pairing to be exact");

/// Transitions sit on the ODD seconds, references on the EVEN ones: transition i
/// at (i+1) x 2 s + 1 s, reference k at (k+1) x 2 s. That interleaving is what
/// makes the references "uniformly spaced" AND automatically >= 50 ms clear
/// (they are ~1 s clear, asserted below over BOTH marks of every transition).
constexpr std::size_t kClickSendEngageBlock = 1u * kClickStepBlocks + kClickSecondBlocks;   // 282
constexpr std::size_t kClickFreezeOnBlock = 2u * kClickStepBlocks + kClickSecondBlocks;     // 470
constexpr std::size_t kClickFreezeOffBlock = 3u * kClickStepBlocks + kClickSecondBlocks;    // 658
constexpr std::size_t kClickWanderOnBlock = 4u * kClickStepBlocks + kClickSecondBlocks;     // 846
constexpr std::size_t kClickWanderOffBlock = 5u * kClickStepBlocks + kClickSecondBlocks;    // 1034
/// See the banner: one shift after the disengage, so its return-gain edge lands
/// at the centre of the disengage's measured window.
constexpr std::size_t kClickSendBypassBlock = kClickWanderOffBlock + kClickShiftBlocks;     // 1039
/// 1222 x 512 = 625 664 samples = 13.03 s. The last reference window ends at
/// 578 496, so the render outlasts every measured span by ~1 s.
constexpr std::size_t kClickTotalBlocks = 6u * kClickStepBlocks + kClickSecondBlocks;       // 1222
constexpr std::size_t kClickTotalSamples = kClickTotalBlocks * kBlockSamples;

/// FR-008's deferred reset MUST be unambiguous at the engage: the send has been
/// bypassed since prepare, and 282 blocks is 144 384 samples against the 96 000
/// of kFxSendDrainMs. A schedule that engaged at ~2 s would sit 256 samples from
/// that boundary and flip code paths under any future timing edit.
static_assert(kClickSendEngageBlock * kBlockSamples
                  > static_cast<std::size_t>(Seraphis::kFxSendDrainMs * kSampleRate / 1000.0),
              "FR-008: the engage must be unambiguously past kFxSendDrainMs of bypass");

struct ClickTransition {
    std::size_t block;
    const char* name;
};

constexpr ClickTransition kClickTransitions[] = {
    {.block = kClickSendEngageBlock, .name = "send engage"},
    {.block = kClickFreezeOnBlock, .name = "freeze on"},
    {.block = kClickFreezeOffBlock, .name = "freeze off"},
    {.block = kClickWanderOnBlock, .name = "wander-bypass engage"},
    {.block = kClickWanderOffBlock, .name = "wander-bypass disengage"},
    {.block = kClickSendBypassBlock, .name = "send bypass"},
};
constexpr std::size_t kClickTransitionCount = 6;
static_assert(sizeof(kClickTransitions) / sizeof(kClickTransitions[0]) == kClickTransitionCount,
              "SC-008: SIX transitions are in scope");

/// Clause 3's "same number of draws on both sides".
constexpr std::size_t kClickReferenceCount = kClickTransitionCount;

[[nodiscard]] constexpr std::size_t clickTestWindowStart(std::size_t i) noexcept {
    return kClickTransitions[i].block * kBlockSamples + kClickWindowShiftSamples
           - kClickHalfWindow;
}

[[nodiscard]] constexpr std::size_t clickRefWindowStart(std::size_t k) noexcept {
    return (k + 1u) * kClickStepBlocks * kBlockSamples;
}

/// "at least 50 ms clear of any transition in the same output domain". BOTH
/// marks are checked - the raw write AND its shifted position - because the
/// reference is supposed to carry the render's ordinary programme material and
/// neither mark does.
[[nodiscard]] constexpr bool clickWindowIsClearOf(std::size_t begin, std::size_t mark) noexcept {
    const std::size_t end = begin + kClickWindowSamples;
    return (mark + kClickFiftyMs <= begin) || (mark >= end + kClickFiftyMs);
}

[[nodiscard]] constexpr bool clickReferencesAreQuiescent() noexcept {
    for (std::size_t k = 0; k < kClickReferenceCount; ++k) {
        const std::size_t begin = clickRefWindowStart(k);
        if (begin + kClickWindowSamples > kClickTotalSamples) {
            return false;
        }
        for (auto kClickTransition : kClickTransitions) {
            const std::size_t event = kClickTransition.block * kBlockSamples;
            if (!clickWindowIsClearOf(begin, event)
                || !clickWindowIsClearOf(begin, event + kClickWindowShiftSamples)) {
                return false;
            }
        }
    }
    return true;
}
static_assert(clickReferencesAreQuiescent(),
              "SC-008 clause 2: every reference window must lie inside the render and at "
              "least 50 ms clear of every transition's raw AND shifted position");

[[nodiscard]] constexpr bool clickTestWindowsFitTheRender() noexcept {
    for (std::size_t i = 0; i < kClickTransitionCount; ++i) {
        if (clickTestWindowStart(i) + kClickWindowSamples > kClickTotalSamples) {
            return false;
        }
    }
    return true;
}
static_assert(clickTestWindowsFitTheRender(),
              "SC-008 clause 1: every shifted window must be inside the render");

/// The disengage's window has to contain the bypass edge for positive control
/// (b) to be capable of failing at all - see the banner. Asserted rather than
/// left as a comment, so a future re-spacing that breaks the pairing fails here
/// instead of silently turning the control vacuous.
constexpr std::size_t kClickWanderOffIndex = 4;
static_assert(kClickTransitions[kClickWanderOffIndex].block == kClickWanderOffBlock,
              "the pairing assertion below indexes the transition table, so a reordering must "
              "fail here rather than silently point at another transition");
static_assert(kClickSendBypassBlock * kBlockSamples >= clickTestWindowStart(kClickWanderOffIndex)
                  && kClickSendBypassBlock * kBlockSamples
                         < clickTestWindowStart(kClickWanderOffIndex) + kClickWindowSamples,
              "the send-bypass edge must lie inside the wander-disengage window, or SC-008's "
              "positive control (b) has no return-gain edge to measure");

// --- the operating point, every value DERIVED from the shipped ranges --------
/// kDefaultDelayMs = 250 through the registered range. 0.125 is exact in binary
/// floating point. Deliberately NOT 0: at 250 ms the send's first return arrives
/// ~282 ms after the engage, i.e. FAR outside every measured window, so the
/// component's own turn-on (an abrupt arrival no ramp in this phase governs) can
/// never be mistaken for a transition click. At delay 0 that arrival lands ~11 ms
/// from the engage window's edge.
constexpr float kClickDelayMsPlain = Krate::DSP::SpectralDelay::kDefaultDelayMs;
constexpr double kClickDelayNormalized =
    static_cast<double>(kClickDelayMsPlain - Seraphis::kFxDelayTimeMinMs)
    / static_cast<double>(Seraphis::kFxDelayTimeMaxMs - Seraphis::kFxDelayTimeMinMs);
/// Loud, sustaining return - the send must be worth switching off, or the bypass
/// edge positive control (b) exposes is a step of nothing.
constexpr float kClickFeedbackPlain = 0.6f;
constexpr double kClickFeedbackNormalized =
    static_cast<double>(kClickFeedbackPlain) / static_cast<double>(Seraphis::kFxDelayFeedbackMax);
/// Both plain unit rows, both strictly positive and DIFFERENT - so FR-010's
/// mandatory skip really is the thing that flips at the two wander transitions,
/// and the two drift paths stay distinguishable.
constexpr double kClickWanderDepthNormalized = 0.7;
constexpr double kClickAzimuthDepthNormalized = 0.4;
static_assert(kClickWanderDepthNormalized != kClickAzimuthDepthNormalized,
              "FR-024a: the two depths differ so the two drift paths stay distinguishable");
/// kFxWidthId is NEVER written: its C-6 default is exactly kDefaultWidth = 100 %,
/// which is what leaves FR-010's raw predicate answerable by the depths ALONE.
static_assert(Seraphis::kFxWidthDefault == Krate::DSP::MidSideProcessor::kDefaultWidth,
              "FR-010: the wander transitions are driven by the depths only, which requires "
              "the width to sit at the identity the predicate tests for");

/// PER CHANNEL, so positive control (a) knows WHICH channel produced
/// max(reference) and can inject into exactly that window. Identical arithmetic
/// to this TU's maxPerSampleDelta(TapRender, ...) (:1057), which collapses the
/// two channels and therefore cannot answer that question - the case asserts the
/// two agree rather than trusting this comment.
[[nodiscard]] float channelMaxDelta(const std::vector<float>& x, std::size_t first,
                                    std::size_t count) {
    const std::size_t end = std::min(x.size(), first + count);
    float worst = 0.0f;
    for (std::size_t i = std::max<std::size_t>(first, 1u); i < end; ++i) {
        worst = std::max(worst, std::fabs(x[i] - x[i - 1]));
    }
    return worst;
}

struct ClickStats {
    float maxTest = 0.0f;
    float maxRef = 0.0f;
    std::size_t worstTestIndex = 0;
    std::size_t worstRefStart = 0;
    int worstRefChannel = 0;
};

[[nodiscard]] ClickStats measureClicks(const TapRender& r) {
    ClickStats s{};
    for (std::size_t i = 0; i < kClickTransitionCount; ++i) {
        const std::size_t t0 = clickTestWindowStart(i);
        const float d = std::max(channelMaxDelta(r.left, t0, kClickWindowSamples),
                                 channelMaxDelta(r.right, t0, kClickWindowSamples));
        if (d > s.maxTest) {
            s.maxTest = d;
            s.worstTestIndex = i;
        }
    }
    for (std::size_t k = 0; k < kClickReferenceCount; ++k) {
        const std::size_t q0 = clickRefWindowStart(k);
        const float dl = channelMaxDelta(r.left, q0, kClickWindowSamples);
        const float dr = channelMaxDelta(r.right, q0, kClickWindowSamples);
        if (dl > s.maxRef) {
            s.maxRef = dl;
            s.worstRefStart = q0;
            s.worstRefChannel = 0;
        }
        if (dr > s.maxRef) {
            s.maxRef = dr;
            s.worstRefStart = q0;
            s.worstRefChannel = 1;
        }
    }
    return s;
}

/// Clause 4. BIT PATTERN, never std::isnan / std::isinf - see isNonFiniteBits()'
/// own banner at :2603.
[[nodiscard]] bool clickRenderIsFinite(const TapRender& r) {
    return std::ranges::none_of(r.left, [](float v) { return isNonFiniteBits(v); })
           && std::ranges::none_of(r.right, [](float v) { return isNonFiniteBits(v); });
}

struct ClickScriptRender {
    TapRender tap;
    std::size_t sendChunks = 0;
    bool wanderRan = false;            ///< FR-010's RAW predicate, true on any block
    bool wanderEffectiveAtEnd = true;  ///< FR-010a's LATCHED predicate at the end
    std::size_t pushesBeforeFreezeOn = 0;
    std::size_t pushesAfterFreezeOn = 0;
    std::size_t pushesBeforeFreezeOff = 0;
    std::size_t pushesAfterFreezeOff = 0;
};

/// ONE render of the pinned script. `snapReturnRamp` is FR-040 capability 3 and
/// is the ONLY difference between the criterion's arm and positive control (b)'s
/// - "the same render" in the criterion's own words.
[[nodiscard]] ClickScriptRender renderClickScript(bool snapReturnRamp) {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    const ScopedEffectsProbe guard(*fx.proc, /*bypassed*/ false, /*afterOutput*/ false,
                                   /*rampSnap*/ snapReturnRamp);

    // The shift's first term, MEASURED rather than transcribed: FR-033 reports
    // AetherReverb's own latency and nothing else (processor.cpp:1265-1270).
    REQUIRE(static_cast<std::size_t>(fx.proc->getLatencySamples()) == kClickReverbLatency);

    ClickScriptRender out;
    bool wanderRan = false;
    std::size_t beforeOn = 0;
    std::size_t afterOn = 0;
    std::size_t beforeOff = 0;
    std::size_t afterOff = 0;

    out.tap = captureTapRender(
        fx, kClickTotalBlocks, kBlockSamples, [&](std::size_t b, Fixture& f) {
            // Observations of what the PREVIOUS block left behind - the script
            // runs before process(), so at block b these counters cover 0..b-1.
            wanderRan = wanderRan || Probe::wanderRunsRaw(*f.proc);
            if (b == kClickFreezeOnBlock) {
                beforeOn = f.proc->effectsPushCountForTest();
            } else if (b == kClickFreezeOnBlock + 1u) {
                afterOn = f.proc->effectsPushCountForTest();
            } else if (b == kClickFreezeOffBlock) {
                beforeOff = f.proc->effectsPushCountForTest();
            } else if (b == kClickFreezeOffBlock + 1u) {
                afterOff = f.proc->effectsPushCountForTest();
            }

            if (b == 0) {
                f.setParam(Seraphis::kFxDelayTimeId, kClickDelayNormalized);
                f.setParam(Seraphis::kFxDelayFeedbackId, kClickFeedbackNormalized);
                for (const Steinberg::int16 pitch : kEightNoteChord) {
                    f.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
                }
            } else if (b == kClickSendEngageBlock) {
                f.setParam(Seraphis::kFxDelayMixId, 1.0);
            } else if (b == kClickFreezeOnBlock) {
                f.setParam(Seraphis::kFxSpectralFreezeId, 1.0);
            } else if (b == kClickFreezeOffBlock) {
                f.setParam(Seraphis::kFxSpectralFreezeId, 0.0);
            } else if (b == kClickWanderOnBlock) {
                f.setParam(Seraphis::kFxWanderDepthId, kClickWanderDepthNormalized);
                f.setParam(Seraphis::kFxAzimuthDepthId, kClickAzimuthDepthNormalized);
            } else if (b == kClickWanderOffBlock) {
                f.setParam(Seraphis::kFxWanderDepthId, 0.0);
                f.setParam(Seraphis::kFxAzimuthDepthId, 0.0);
            } else if (b == kClickSendBypassBlock) {
                f.setParam(Seraphis::kFxDelayMixId, 0.0);
            }
        });

    out.sendChunks = fx.proc->sendChunkCountForTest();
    out.wanderRan = wanderRan;
    out.wanderEffectiveAtEnd = Probe::wanderRunsEffective(*fx.proc);
    out.pushesBeforeFreezeOn = beforeOn;
    out.pushesAfterFreezeOn = afterOn;
    out.pushesBeforeFreezeOff = beforeOff;
    out.pushesAfterFreezeOff = afterOff;
    return out;
}

}  // namespace

TEST_CASE("Effects transitions are click-free", "[seraphis][effects]") {
    const ClickScriptRender shipped = renderClickScript(/*snapReturnRamp*/ false);

    REQUIRE(shipped.tap.left.size() == kClickTotalSamples);
    REQUIRE(shipped.tap.right.size() == kClickTotalSamples);

    // --- the script's own preconditions --------------------------------------
    // Not decoration: without them the whole case could pass on a render in which
    // the send never ran, the wander stage was skipped end to end, or the freeze
    // toggles never reached the component - i.e. on a render carrying none of the
    // six transitions it claims to measure.
    REQUIRE(shipped.tap.outputPeak > 0.0f);
    REQUIRE(shipped.sendChunks > std::size_t{0});
    REQUIRE(shipped.wanderRan);
    CHECK_FALSE(shipped.wanderEffectiveAtEnd);
    CHECK(shipped.pushesAfterFreezeOn > shipped.pushesBeforeFreezeOn);
    CHECK(shipped.pushesAfterFreezeOff > shipped.pushesBeforeFreezeOff);

    // --- CLAUSE 4: no non-finite sample, BY BIT PATTERN ----------------------
    CHECK(clickRenderIsFinite(shipped.tap));

    // --- CLAUSES 1-3 ---------------------------------------------------------
    const ClickStats stats = measureClicks(shipped.tap);

    // The per-channel statistic this case measures with IS the one T017's FR-010a
    // section measures with; asserted on the window that produced max(reference)
    // rather than assumed.
    const float perChannelWorst = std::max(
        channelMaxDelta(shipped.tap.left, stats.worstRefStart, kClickWindowSamples),
        channelMaxDelta(shipped.tap.right, stats.worstRefStart, kClickWindowSamples));
    const float collapsedWorst =
        maxPerSampleDelta(shipped.tap, stats.worstRefStart, kClickWindowSamples);
    REQUIRE(perChannelWorst == collapsedWorst);

    // Non-vacuity: a bound of 0 would be met by a silent render, and "quiescent"
    // in clause 2 means exactly that - not silence.
    REQUIRE(stats.maxRef > 0.0f);
    REQUIRE(stats.maxTest > 0.0f);

    const float bound = kClickBoundFactor * stats.maxRef;
    INFO("SC-008: worst test window is \""
         << kClickTransitions[stats.worstTestIndex].name << "\" at " << stats.maxTest
         << " against bound " << bound << " (" << kClickBoundFactor << " x max reference "
         << stats.maxRef << ", from sample " << stats.worstRefStart << " channel "
         << stats.worstRefChannel << "); " << kClickTransitionCount << " draws each side");
    CHECK(stats.maxTest <= bound);

    // =========================================================================
    // POSITIVE CONTROL (a) - DETECTOR WIRING
    // =========================================================================
    // The same statistic over a NON-TRANSITION window, with a deliberately
    // injected one-sample step of 2 x THAT WINDOW'S OWN maxPerSampleDelta, must
    // EXCEED the bound. The window chosen is the one that produced
    // max(reference), so its own maximum IS stats.maxRef and the injected step is
    // 2 x stats.maxRef against a 1.5 x stats.maxRef bound - the control cannot
    // pass or fail by accident of which window was picked.
    {
        const std::vector<float>& src =
            (stats.worstRefChannel == 0) ? shipped.tap.left : shipped.tap.right;
        REQUIRE(stats.worstRefStart >= 1u);
        REQUIRE(stats.worstRefStart + kClickWindowSamples <= src.size());

        // ONE EXTRA LEADING SAMPLE, measured from index 1, so the copy sees
        // exactly the deltas measureClicks() saw - including the delta across the
        // window's own first sample. Without it the copy's statistic would be
        // <= the original's and the equality below (which is what makes "2 x its
        // OWN maximum" equal "2 x max(reference)") would not hold.
        const auto from = static_cast<std::ptrdiff_t>(stats.worstRefStart - 1u);
        const auto to = static_cast<std::ptrdiff_t>(stats.worstRefStart + kClickWindowSamples);
        std::vector<float> injected(src.begin() + from, src.begin() + to);
        REQUIRE(injected.size() == kClickWindowSamples + 1u);
        REQUIRE(channelMaxDelta(injected, 1u, kClickWindowSamples) == stats.maxRef);

        const std::size_t mid = 1u + (kClickWindowSamples / 2u);
        injected[mid] = injected[mid - 1u] + 2.0f * stats.maxRef;

        const float detected = channelMaxDelta(injected, 1u, kClickWindowSamples);
        INFO("SC-008 control (a): injected one-sample step of " << (2.0f * stats.maxRef)
                                                                << " gave " << detected
                                                                << " against bound " << bound);
        CHECK(detected > bound);
    }

    // =========================================================================
    // POSITIVE CONTROL (b) - CRITERION WIRING
    // =========================================================================
    // With FR-040's capability 3 snapping the FR-008/FR-009 return-gain ramp to
    // instant (processor.cpp:2030-2032), the SAME render must FAIL clause 3.
    //
    // What it measures: the send is bypassed at kClickSendBypassBlock while its
    // 250 ms / 0.6-feedback return is at full level, and the return gain is the
    // ONLY thing standing between that return and the bus. Ramped, the gain walks
    // down in 64-sample treads (D-6 forces the sub-slice subdivision for the whole
    // of the ramp), each tread stepping the return by at most
    // 1 - e^(-64/960) = 6.5 % of its amplitude. Snapped, the whole return is
    // removed in ONE sample - and that edge sits at the centre of the
    // wander-disengage window by construction (see the banner), so it is a
    // measured statistic and not audio nobody looks at.
    //
    // If this ever stops failing, the criterion has stopped being able to see the
    // ramp at all and clause 3's green is worthless.
    {
        const ClickScriptRender snapped = renderClickScript(/*snapReturnRamp*/ true);
        REQUIRE(snapped.tap.left.size() == kClickTotalSamples);
        REQUIRE(snapped.sendChunks > std::size_t{0});
        REQUIRE(snapped.wanderRan);

        const ClickStats snappedStats = measureClicks(snapped.tap);
        REQUIRE(snappedStats.maxRef > 0.0f);

        const float snappedBound = kClickBoundFactor * snappedStats.maxRef;
        INFO("SC-008 control (b): snapped worst test window is \""
             << kClickTransitions[snappedStats.worstTestIndex].name << "\" at "
             << snappedStats.maxTest << " against bound " << snappedBound << " (max reference "
             << snappedStats.maxRef << "); the shipped arm scored " << stats.maxTest
             << " against " << bound);
        CHECK_FALSE(snappedStats.maxTest <= snappedBound);
    }
}

