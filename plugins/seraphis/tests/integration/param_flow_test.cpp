// ==============================================================================
// Seraphis - Parameter-to-engine flow tests (T022)
// ==============================================================================
// TEST_CASE("Seraphis_ParamFlowReachesEngine") covers FR-024a and FR-044 through
// the two criteria they answer to:
//
//   SC-019 - the three shipped global parameters REACH THE CHAIN. Without this
//            case an implementation whose masterGain atomic is written and never
//            multiplied in, and whose polyphony atomic never reaches
//            SeraphisEngine::setPolyphony (dsp/.../seraphis_engine.h:321),
//            satisfies EVERY OTHER criterion in this phase.
//   SC-027 - the soft-limit toggle has a MEASURABLE effect, in the SECTION named
//            Seraphis_SoftLimitIsMeasurable. ITS CLAUSE 1 IS NOT REACHABLE IN
//            PHASE 8 and the section says so out loud, with the measured ladder
//            that proves it - see the "SC-027 MEASURED RESULT" block below.
//            FR-044 is recorded in compliance.md as verified by code inspection
//            plus a sub-tolerance measurement, never silently marked green.
//
// The case injects no NaN/Inf; it is in ../CMakeLists.txt's
// -fno-fast-math list only because the plan names it there (plan 4.4).
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <render_fingerprint.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;

/// 4 s at 48 kHz - the render length SC-019 clause 1 names.
constexpr std::size_t kFourSeconds = 192000;

/// 2 s at 48 kHz. Used wherever the criterion does not fix the length: long
/// enough for the 1024-sample spectral-diffusion latency and the reverb build-up
/// to be well inside the compared region, half the render cost of 4 s.
constexpr std::size_t kTwoSeconds = 96000;

/// 16 s at 48 kHz - the SC-027 ladder's top rung. Beyond this the level does not
/// climb any further (see the ladder's provenance note).
constexpr std::size_t kSixteenSeconds = 768000;

/// TruePeakLimiter::ceilingLin_ for the default -1 dBFS ceiling
/// (dsp/include/krate/dsp/processors/true_peak_limiter.h:46, :168). SC-019
/// clause 2 is only meaningful BELOW this: above it the limiter compresses the
/// ratio it is trying to measure.
constexpr float kLimiterCeilingLin = 0.8912509f;

// -----------------------------------------------------------------------------
// Aggregates
// -----------------------------------------------------------------------------

[[nodiscard]] float maxAbs(const std::vector<float>& v, std::size_t from = 0) {
    float peak = 0.0f;
    for (std::size_t i = from; i < v.size(); ++i) {
        peak = std::max(peak, std::abs(v[i]));
    }
    return peak;
}

/// Largest per-sample |a - b| over the common prefix.
[[nodiscard]] float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float worst = 0.0f;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

/// RMS accumulated in double, so the aggregate is not itself a source of
/// cross-toolchain spread (the render_fingerprint.h convention).
[[nodiscard]] double rmsOf(const std::vector<float>& v) {
    if (v.empty()) {
        return 0.0;
    }
    double sumSquares = 0.0;
    for (const float s : v) {
        const double d = static_cast<double>(s);
        sumSquares += d * d;
    }
    return std::sqrt(sumSquares / static_cast<double>(v.size()));
}

/// RMS of (a - b) over the common prefix.
[[nodiscard]] double rmsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n == 0) {
        return 0.0;
    }
    double sumSquares = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSquares += d * d;
    }
    return std::sqrt(sumSquares / static_cast<double>(n));
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

/// Everything the Phase 8 parameter surface can do to change the level reaching
/// the output stage: three globals plus how many notes are held.
struct Drive {
    double masterGainNorm = 0.5;             ///< FR-043: linear gain = value * 2
    double softLimitNorm = 1.0;              ///< 1.0 -> saturation 0.15, 0.0 -> 0.0
    double polyphonyNorm = 7.0 / 15.0;       ///< normalized 7/15 == 8 voices (default)
    std::span<const Steinberg::int16> pitches{};
    float velocity = 0.8f;
    std::size_t totalSamples = 0;
};

/// One complete render of a held chord through Processor::process().
///
/// All three global parameters are delivered as automation queues on BLOCK 0,
/// i.e. before a single sample has been produced. That is deliberate for the
/// master gain: processParameterChanges() runs at the top of process(), so the
/// atomic is already at its final value when FR-024a clause 3's first-block SNAP
/// reads it. A ramped-from-default implementation is exactly what SC-019
/// clause 1 is built to fail.
///
/// The fixture (and with it the ~33 MB of per-voice capture rings the engine
/// allocates at prepare) is destroyed when this returns, so at most one engine
/// is alive at a time however many renders a SECTION performs.
[[nodiscard]] Render renderHeldChord(const Drive& drive) {
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    const std::size_t totalSamples = drive.totalSamples;
    const std::size_t blockSize = static_cast<std::size_t>(kBlock);
    const std::size_t numBlocks = (totalSamples + blockSize - 1u) / blockSize;

    fx.renderBlocks(numBlocks, blockSize,
                    [&](std::size_t b, Krate::Test::EventList& events,
                        SeraphisTest::ParameterChanges& params) {
                        if (b != 0) {
                            return;
                        }
                        params.addQueue(Seraphis::kMasterGainId)
                            .addTestPoint(0, drive.masterGainNorm);
                        params.addQueue(Seraphis::kSoftLimitId)
                            .addTestPoint(0, drive.softLimitNorm);
                        params.addQueue(Seraphis::kPolyphonyId)
                            .addTestPoint(0, drive.polyphonyNorm);
                        for (const Steinberg::int16 pitch : drive.pitches) {
                            events.addNoteOn(pitch, drive.velocity, 0);
                        }
                    });

    REQUIRE(fx.capturedL.size() >= totalSamples);
    REQUIRE(fx.capturedR.size() >= totalSamples);
    REQUIRE(fx.checkCanaries());

    const auto span = static_cast<std::ptrdiff_t>(totalSamples);
    Render out;
    out.left.assign(fx.capturedL.begin(), fx.capturedL.begin() + span);
    out.right.assign(fx.capturedR.begin(), fx.capturedR.begin() + span);
    return out;
}

// -----------------------------------------------------------------------------
// Hand-authored state streams (SC-019 clause 3)
// -----------------------------------------------------------------------------
// The layout is plan 3.4's fixed 36 bytes:
//   0 int32 version | 4 float masterGain | 8 int32 polyphony | 12 int32 softLimit
//  16 dream | 20 bloom | 24 dissolve | 28 gravity | 32 entropy
//
// Authoring the bytes here rather than going through getState() is what lets the
// corrupt-stream sub-clause write a polyphony value (0, 20) that no legal
// getState() would ever emit.
// -----------------------------------------------------------------------------

struct StreamReleaser {
    void operator()(Steinberg::MemoryStream* s) const noexcept {
        if (s != nullptr) {
            s->release();
        }
    }
};
using StreamPtr = std::unique_ptr<Steinberg::MemoryStream, StreamReleaser>;

[[nodiscard]] StreamPtr makeStateStream(Steinberg::int32 polyphony) {
    StreamPtr s(new Steinberg::MemoryStream());
    {
        // kLittleEndian is a MACRO (`#define kLittleEndian 0`,
        // pluginterfaces/base/fplatform.h:20), not a Steinberg-namespace
        // constant: qualifying it produces `Steinberg::0`.
        Steinberg::IBStreamer writer(s.get(), kLittleEndian);
        writer.writeInt32(Seraphis::kCurrentStateVersion);
        writer.writeFloat(1.0f);       // masterGain - the registered default
        writer.writeInt32(polyphony);  // THE FIELD UNDER TEST, written RAW
        writer.writeInt32(1);          // softLimit on
        writer.writeFloat(0.0f);       // dream
        writer.writeFloat(0.0f);       // bloom
        writer.writeFloat(0.0f);       // dissolve
        writer.writeFloat(0.5f);       // gravity - the registered default
        writer.writeFloat(0.0f);       // entropy
    }
    s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return s;
}

[[nodiscard]] Steinberg::Vst::ProcessSetup makeSetup() {
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate = kSampleRate;
    return setup;
}

[[nodiscard]] Krate::DSP::SeraphisEngine& engineOf(SeraphisTest::ProcessorFixture& fx) {
    Krate::DSP::SeraphisEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    return *engine;
}

/// The denormalisation FR-043 ships, restated independently of the header under
/// test so the assertion is not "the implementation equals itself"
/// (parameters/global_params.h:86-88).
[[nodiscard]] std::size_t expectedPolyphony(double normalized) {
    return static_cast<std::size_t>(
        std::clamp(static_cast<int>(normalized * 15.0 + 1.0 + 0.5), 1, 16));
}

// -----------------------------------------------------------------------------
// SC-019 clause 1/2 script - ONE held note, moderate velocity.
// -----------------------------------------------------------------------------
constexpr Steinberg::int16 kSingleNote[] = {60};
constexpr float kSingleVelocity = 0.8f;

// -----------------------------------------------------------------------------
// SC-019 clause 1, snap-seam sub-section
// -----------------------------------------------------------------------------
// When the master gain snaps to 0 with the chain already ringing, the wet
// buffers go to exactly zero immediately - but processOutputStage still runs
// AFTER the multiply (seraphis_engine.h:513-522), and its TapeSaturator carries
// pre-emphasis, de-emphasis and DC-blocker state from the loud material
// (tape_saturator.h:390-398). Those linear filters flush their own state into
// the output for a few tens of milliseconds. That residue is NOT master gain
// leaking through; it is the output stage emptying.
//
// RE-MEASURED 2026-07-31 (MSVC / windows-x64-release, 48 kHz, this exact script),
// after ContinuousBody's FR-033a excitation compensation raised the whole chain
// to its documented level. BOTH arms scale with the level of the material the
// output stage is flushing, so both moved and the discrimination is intact; the
// broken arm was re-measured the same way the original was, by temporarily
// replacing snapTo with setTarget at processor.cpp:374 and reverting:
//   snapTo   (shipped)  peak[0..] = 3.0e-4 L / 7.0e-5 R; peak[0.5 s..] = 0.0
//   setTarget (BROKEN)  peak[0..] = 7.22e-3 L / 1.138e-2 R
// (was, pre-FR-033a: shipped 1.16e-5; broken 2.8e-4 L / 4.3e-4 R.)
// The two arms differ by 24x on L and 163x on R, so a bound of 1.2e-3 - 4x above
// the shipped figure and 6x below the broken one - discriminates them with
// headroom on both sides, the same margins the original bound carried.
constexpr float kSnapSeamFlushBound = 1.2e-3f;

/// 0.5 s at 48 kHz - well past the output stage's flush (measured to be over
/// within ~2000 samples). From here on the shipped implementation measures
/// EXACTLY 0.0f, which is the literal SC-019 clause 1 bound.
constexpr std::size_t kFlushTail = 24000;

/// Peaks for SC-019 clause 2 are measured from here on, NOT from sample 0.
///
/// Both arms of that clause run with the soft limit OFF so the output stage is
/// linear and the ratio is exactly 2.0 in exact arithmetic. Getting there costs
/// a transient: the push necessarily lands AFTER SeraphisEngine::prepare() has
/// already installed kOutputSaturation, and post-prepare
/// TapeSaturator::setSaturation RAMPS rather than snaps
/// (tape_saturator.h:248-252, kDefaultSmoothingMs = 5.0f at :88) - the known
/// residual recorded at src/processor/processor.cpp's setupProcessing(). During
/// that ramp the stage is nonlinear, so the two arms are not exact scalings of
/// one another. 0.25 s is 50x the 5 ms ramp and comfortably past the tape
/// saturator's pre/de-emphasis and DC-blocker settling.
constexpr std::size_t kPeakWindowStart = 12000;  // 0.25 s @ 48 kHz

// -----------------------------------------------------------------------------
// SC-027 drive ladder
// -----------------------------------------------------------------------------
// A 0.15 tape-saturation amount only differs from 0.0 by roughly
// 0.15 * x^3 / 3 (TapeSaturator's Simple model blends linear with tanh,
// tape_saturator.h:418-424, and the engine ships mix = 1.0, drive = 0 dB,
// bias = 0 at seraphis_engine.h:228-233). That is ~5e-5 at |x| = 0.1 - already
// BELOW kSampleTolerance - so SC-027 explicitly requires the level to be driven
// up until its non-vacuity clause passes, and requires every level tried to be
// reported rather than the failures quietly dropped.
//
// The ladder below is EVERY lever the Phase 8 parameter surface has: number of
// held notes (bounded by polyphony), velocity, polyphony (max 16), and master
// gain (max linear 2.0 -- FR-043's `value * 2.0`). There is no further reachable
// level; if the top rung still does not clear kSampleTolerance, FR-044 is
// recorded in compliance.md as verified by CODE INSPECTION ONLY, with these
// measured deltas attached.

constexpr Steinberg::int16 kSixNoteChord[] = {48, 55, 60, 64, 67, 72};
constexpr Steinberg::int16 kSixteenNoteChord[] = {36, 40, 43, 48, 52, 55, 60, 64,
                                                  67, 72, 76, 79, 84, 88, 91, 96};

/// Normalized kPolyphonyId for 16 voices (FR-043: clamp(int(v*15 + 1.5), 1, 16)).
constexpr double kPolyphonyNorm16 = 1.0;
/// Normalized kPolyphonyId for the registered default of 8 voices.
constexpr double kPolyphonyNorm8 = 7.0 / 15.0;

struct DriveRung {
    const char* name;
    std::span<const Steinberg::int16> pitches;
    float velocity;
    double masterGainNorm;
    double polyphonyNorm;
    std::size_t totalSamples;
};

// -----------------------------------------------------------------------------
// SC-027 MEASURED RESULT - READ THIS BEFORE CHANGING EITHER THRESHOLD BELOW
// -----------------------------------------------------------------------------
// *** REWRITTEN 2026-07-31. SC-027 CLAUSE 1 PRIMARY CRITERION IS NOW MET AND
//     THE INSPECTION FALLBACK IS RETIRED. ***
//
// The shipped tree could not reach clause 1's `maxAbsDiff > kSampleTolerance`
// (1e-4) - the loudest configuration the Phase 8 surface could produce measured
// 1.46e-7, three orders short - so FR-044 was recorded in compliance.md as
// verified by inspection plus a sub-tolerance measurement, guarded by a
// deliberate tripwire (`REQUIRE(bestMaxAbsDiff < kSampleTolerance)`) whose whole
// purpose was to fail the day the bound became reachable.
//
// IT FAILED. ContinuousBody's FR-033a excitation compensation (phase-owner
// gain-staging ruling, specs/seraphis-phase4-continuous-body/spec.md) made the
// body deliver its documented level instead of running 30-40 dB under it, and
// the chain that fed the saturator |x| ~ 0.0142 now feeds it |x| up to the
// limiter ceiling. The tripwire read 0.0270903 against 1e-4. Per its own
// instruction the reduced form is deleted and clause 1 is asserted as specified.
//
// MEASURED 2026-07-31, MSVC / windows-x64-release, 48 kHz, 512-sample blocks,
// engine and reverb seed 1, Phase 8 macro defaults, the SAME ladder (max over
// L and R), with the pre-FR-033a maxAbsDiff alongside:
//
//   rung                                       pre-out rms  peak     maxAbsDiff  relRmsDiff  relRmsDrop  (was)
//   L0  1 note,  vel 0.8, gain x1, poly  8,  2 s   5.20e-3  1.80e-2  2.96e-7     6.04e-6     +3.26e-6    (1.05e-9)
//   L1  6 notes, vel 1.0, gain x2, poly  8,  2 s   2.50e-2  1.23e-1  9.02e-5     2.58e-4     +1.28e-4    (1.82e-8)
//   L2 16 notes, vel 1.0, gain x2, poly 16,  2 s   3.19e-2  1.76e-1  2.80e-4     4.11e-4     +3.09e-4    (9.59e-8)
//   L3 16 notes, vel 1.0, gain x2, poly 16,  4 s   8.27e-2  4.46e-1  4.20e-3     2.43e-3     +1.50e-3    (1.25e-7)
//   L4 16 notes, vel 1.0, gain x2, poly 16, 16 s   2.24e-1  8.91e-1  2.71e-2     1.13e-2     -1.89e-3    (1.46e-7)
//
// THE SIGN FLIP ON L4 IS REAL, EXPLAINED, AND IS WHY CLAUSE 3 NO LONGER USES THE
// LOUDEST RUNG. L4's pre-output peak is 0.891251 - EXACTLY TruePeakLimiter's
// ceiling - so on that rung the LIMITER is the actor: with the saturator ON the
// peaks it has to catch are smaller, it applies LESS reduction, and the on-arm
// RMS comes out 0.19 % HIGHER (0.224917 vs 0.224492). That is the limiter
// compensating, not an inverted saturator mapping. Clause 3's direction test
// therefore runs on the loudest rung whose pre-output peak is still clear of the
// ceiling (L3, peak 0.446, drop +1.50e-3), where the saturator alone decides the
// level. Clauses 1 and 2 keep using the loudest rung outright - both are
// symmetric magnitudes and the limiter cannot flip their sign.
//
// The direction clause is still measured away from the low end for the reason
// the original ladder recorded: at L0's level the on/off difference is dominated
// by filter-state noise rather than by the cubic term, so the drop only becomes
// coherent from L1 up.
//
// What is STILL a real, non-vacuous detector, unchanged: both arms are rendered
// by the SAME binary, from the SAME seed, with the SAME script, and differ ONLY
// in the value handed to SeraphisEngine::setOutputSaturation. A soft-limit
// parameter that never reached the engine yields a difference of EXACTLY 0.0f.
// -----------------------------------------------------------------------------

/// MEASURED (L3, the direction rung): 2.43e-3; L4 reads 1.13e-2 and every rung
/// from L2 up is >= 4.11e-4. Floor set to ~1/2 of the L3 figure. This is SC-027
/// clause 2's named, measured constant. It was 1.3e-6 while the chain ran
/// 30-40 dB quiet.
constexpr double kSoftLimitRelRmsFloor = 1.2e-3;

/// MEASURED (L3, the direction rung): +1.50e-3. Floor set to ~1/3 of it. It was
/// 4.0e-7 while the chain ran 30-40 dB quiet.
///
/// WHY THIS EXISTS AND THE ONES ABOVE ARE NOT ENOUGH: maxAbsDiff and rmsDiff are
/// both SYMMETRIC in (on, off) - swap the two arms and every number in the ladder
/// is bit-identical - so neither can distinguish FR-044's mapping
/// (on -> kOutputSaturation = 0.15f, off -> 0.0f, seraphis_engine.h:566, :142)
/// from its INVERSE. A processor that pushed 0.0f for `on` and 0.15f for `off`
/// passed both of them. This constant closes that hole: the quantity is signed,
/// and the shipped saturator attenuates (TapeSaturator's Simple model blends
/// linear with tanh at mix = 1.0, drive 0 dB - tape_saturator.h:418-424), so an
/// inverted mapping produces a NEGATIVE drop and fails here.
constexpr double kSoftLimitRelRmsDropFloor = 5.0e-4;

/// Clause 3's rung filter: the direction test only uses rungs whose pre-output
/// peak is at or below this fraction of the limiter ceiling, so the limiter is
/// demonstrably not the actor deciding the level. L3 sits at 0.50 of it.
constexpr float kSoftLimitDirectionCeilingFrac = 0.75f;

}  // namespace

TEST_CASE("Seraphis_ParamFlowReachesEngine", "[seraphis][integration]") {

    // =========================================================================
    // SC-019 clause 1 - MASTER GAIN SILENCES
    // =========================================================================
    // Peak < 1e-6 over the WHOLE 4 s render, FROM SAMPLE 0, with no "after the
    // first N ms" allowance.
    //
    // THIS ASSERTION ALONE DOES NOT TEST THE SNAP, and the criterion's claim
    // that "a ramped-from-default implementation fails by design" is FALSE for a
    // render that starts from a fresh prepare - MEASURED: replacing
    // masterGain_.snapTo(gainTarget) with setTarget(gainTarget) in
    // src/processor/processor.cpp leaves this section GREEN. The reason is that
    // the seam is armed only at setupProcessing()/setActive(true), and at that
    // moment the engine and the reverb are empty, so the ~20 ms the smoother
    // spends ramping from 1.0 down to 0 multiplies a signal that is itself
    // below 1e-6. The ramp has nothing to let through.
    //
    // The second half of this section therefore arms the seam WITH THE CHAIN
    // ALREADY RINGING - setActive(true) on an already-active processor re-arms
    // anySamplesSincePrepare_ without silencing anything (FR-032; only
    // setActive(false) calls engine_->silence()/reverb_->reset()) - which is the
    // only reachable state where snap and ramp differ audibly. VERIFIED: with
    // setTarget in place of snapTo, that half fails; with snapTo it passes.
    // =========================================================================
    SECTION("clause 1: master gain 0 silences the WHOLE render, from sample 0") {
        const Render silent = renderHeldChord(Drive{.masterGainNorm = 0.0,
                                                    .pitches = kSingleNote,
                                                    .velocity = kSingleVelocity,
                                                    .totalSamples = kFourSeconds});

        CHECK(maxAbs(silent.left) < 1.0e-6f);
        CHECK(maxAbs(silent.right) < 1.0e-6f);

        // NON-VACUITY: the same script at unity gain must NOT be silent,
        // otherwise the assertion above passes for an engine that renders
        // nothing at all. Two seconds is enough - SC-019 clause 2 below renders
        // the identical script for the same length and requires a healthy peak.
        const Render audible = renderHeldChord(Drive{.masterGainNorm = 0.5,
                                                     .pitches = kSingleNote,
                                                     .velocity = kSingleVelocity,
                                                     .totalSamples = kTwoSeconds});
        REQUIRE(maxAbs(audible.left) > 1.0e-4f);
        REQUIRE(maxAbs(audible.right) > 1.0e-4f);
    }

    // =========================================================================
    // SC-019 clause 1, SECOND HALF - THE SNAP ITSELF (FR-024a clause 3)
    // =========================================================================
    SECTION("clause 1: the snap seam silences a ringing chain from sample 0") {
        const std::size_t blockSize = static_cast<std::size_t>(kBlock);

        SeraphisTest::ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        // 1. Warm the chain up at unity gain with the note held.
        fx.renderBlocks(kTwoSeconds / blockSize, blockSize,
                        [](std::size_t b, Krate::Test::EventList& events,
                           SeraphisTest::ParameterChanges& params) {
                            if (b != 0) {
                                return;
                            }
                            params.addQueue(Seraphis::kMasterGainId).addTestPoint(0, 0.5);
                            events.addNoteOn(kSingleNote[0], kSingleVelocity, 0);
                        });

        // NON-VACUITY: the chain must actually be RINGING at the seam, or the
        // ramp below would again have nothing to let through and the assertion
        // would be as vacuous as the fresh-prepare one.
        const std::vector<float> tailL(fx.capturedL.end() - static_cast<std::ptrdiff_t>(blockSize),
                                       fx.capturedL.end());
        const std::vector<float> tailR(fx.capturedR.end() - static_cast<std::ptrdiff_t>(blockSize),
                                       fx.capturedR.end());
        REQUIRE(maxAbs(tailL) > 1.0e-4f);
        REQUIRE(maxAbs(tailR) > 1.0e-4f);

        // 2. Re-arm the seam WITHOUT silencing: setActive(true) on an already
        //    active processor sets anySamplesSincePrepare_ = false and does
        //    nothing else (FR-032). engine_->silence() lives on the FALSE branch.
        fx.capturedL.clear();
        fx.capturedR.clear();
        REQUIRE(fx.proc->setActive(true) == Steinberg::kResultOk);

        // 3. Master gain 0 from the very next block. A SNAP zeroes the wet
        //    buffers from sample 0 and only the output stage's own state flushes
        //    (measured peak 3.0e-4); a ramp from the smoother's current 1.0
        //    lets ~20 ms of the ringing chain through (measured 7.22e-3 /
        //    1.138e-2), which is 24x the snap figure and 6x this bound.
        fx.renderBlocks(kTwoSeconds / blockSize, blockSize,
                        [](std::size_t b, Krate::Test::EventList& /*events*/,
                           SeraphisTest::ParameterChanges& params) {
                            if (b != 0) {
                                return;
                            }
                            params.addQueue(Seraphis::kMasterGainId).addTestPoint(0, 0.0);
                        });

        WARN("snap seam: peak[0..] L=" << maxAbs(fx.capturedL) << " R=" << maxAbs(fx.capturedR)
                                       << " | peak[0.5 s..] L=" << maxAbs(fx.capturedL, kFlushTail)
                                       << " R=" << maxAbs(fx.capturedR, kFlushTail));

        // (a) THE SNAP. Bounded from sample 0, at a level that discriminates
        //     snap from ramp by more than 5x - see kSnapSeamFlushBound.
        CHECK(maxAbs(fx.capturedL) < kSnapSeamFlushBound);
        CHECK(maxAbs(fx.capturedR) < kSnapSeamFlushBound);

        // (b) THE SILENCE. Once the output stage has flushed, gain 0 means
        //     EXACTLY zero - the literal SC-019 clause 1 bound, and measured at
        //     0.0f on both channels.
        CHECK(maxAbs(fx.capturedL, kFlushTail) < 1.0e-6f);
        CHECK(maxAbs(fx.capturedR, kFlushTail) < 1.0e-6f);
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // SC-019 clause 2 - MASTER GAIN SCALES
    // =========================================================================
    // peak(norm 1.0) / peak(norm 0.5) == 2.0 +/- 5 %.
    //
    // Both arms run with the SOFT LIMIT OFF. That is not a convenience: with the
    // tape saturator engaged the output stage is nonlinear by construction and a
    // *correct* implementation would not produce a 2.0 ratio. With saturation 0
    // the remaining stage - pre/de-emphasis, DC blocker, and a true-peak limiter
    // that is exactly unity below its ceiling (true_peak_limiter.h:150-160) - is
    // linear, so the ratio is exactly 2.0 in exact arithmetic.
    //
    // The peak is asserted to stay BELOW the limiter ceiling; above it the ratio
    // is compressed and the criterion measures the limiter instead of the gain.
    // =========================================================================
    SECTION("clause 2: master gain scales the render by exactly the gain ratio") {
        const Render unity = renderHeldChord(Drive{.masterGainNorm = 0.5,
                                                   .softLimitNorm = 0.0,
                                                   .pitches = kSingleNote,
                                                   .velocity = kSingleVelocity,
                                                   .totalSamples = kTwoSeconds});
        const Render doubled = renderHeldChord(Drive{.masterGainNorm = 1.0,
                                                     .softLimitNorm = 0.0,
                                                     .pitches = kSingleNote,
                                                     .velocity = kSingleVelocity,
                                                     .totalSamples = kTwoSeconds});

        const float peakUnityL = maxAbs(unity.left, kPeakWindowStart);
        const float peakUnityR = maxAbs(unity.right, kPeakWindowStart);
        const float peakDoubledL = maxAbs(doubled.left, kPeakWindowStart);
        const float peakDoubledR = maxAbs(doubled.right, kPeakWindowStart);

        WARN("SC-019 clause 2 levels: peak(norm 0.5) L=" << peakUnityL << " R=" << peakUnityR
                                                         << ", peak(norm 1.0) L=" << peakDoubledL
                                                         << " R=" << peakDoubledR);

        // Non-vacuity, and the "limiter must not engage" precondition.
        REQUIRE(peakUnityL > 1.0e-4f);
        REQUIRE(peakUnityR > 1.0e-4f);
        REQUIRE(peakDoubledL < kLimiterCeilingLin);
        REQUIRE(peakDoubledR < kLimiterCeilingLin);

        const double ratioL = static_cast<double>(peakDoubledL) / static_cast<double>(peakUnityL);
        const double ratioR = static_cast<double>(peakDoubledR) / static_cast<double>(peakUnityR);
        INFO("peak ratio L=" << ratioL << " R=" << ratioR);
        CHECK(std::abs(ratioL - 2.0) <= 0.10);  // 2.0 +/- 5 %
        CHECK(std::abs(ratioR - 2.0) <= 0.10);
    }

    // =========================================================================
    // SC-019 clause 3a/3b - POLYPHONY IS SEEDED AT PREPARE, AND TRACKED THERE
    // =========================================================================
    // setState() may legally arrive BEFORE setupProcessing() (FR-023 clause 2),
    // which is the ordering a preset load produces. The engine must therefore
    // hold the STREAM'S voice count before a single sample is rendered - not the
    // SeraphisEngineConfig struct default of 8 - and the first process() after
    // that prepare must NOT re-call setPolyphony (FR-023 clause 3), which would
    // re-arm sumGain_ (seraphis_engine.h:349) on every host prepare.
    // =========================================================================
    SECTION("clause 3a/3b: polyphony is seeded at prepare and not re-pushed on block 0") {
        SeraphisTest::ProcessorFixture fx;
        REQUIRE(fx.proc->initialize(nullptr) == Steinberg::kResultOk);

        StreamPtr state = makeStateStream(4);
        REQUIRE(fx.proc->setState(state.get()) == Steinberg::kResultOk);

        Steinberg::Vst::ProcessSetup setup = makeSetup();
        REQUIRE(fx.proc->setupProcessing(setup) == Steinberg::kResultOk);
        REQUIRE(fx.proc->setActive(true) == Steinberg::kResultOk);

        Krate::DSP::SeraphisEngine& engine = engineOf(fx);

        // BEFORE any process() call. 4, not the struct default 8.
        REQUIRE(engine.getPolyphony() == std::size_t{4});
        REQUIRE(fx.proc->setPolyphonyCallCountForTest() == std::size_t{0});

        // ...and the first block must not touch it.
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(engine.getPolyphony() == std::size_t{4});
        CHECK(fx.proc->setPolyphonyCallCountForTest() == std::size_t{0});
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // SC-019 clause 3c - PUSHED ON CHANGE, AND ONLY ON CHANGE
    // =========================================================================
    SECTION("clause 3c: polyphony reaches the engine on change, and only on change") {
        SeraphisTest::ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

        Krate::DSP::SeraphisEngine& engine = engineOf(fx);
        REQUIRE(engine.getPolyphony() == std::size_t{8});  // the registered default
        REQUIRE(fx.proc->setPolyphonyCallCountForTest() == std::size_t{0});

        std::size_t expectedCalls = 0;
        for (const double normalized : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            INFO("kPolyphonyId normalized = " << normalized);
            const std::size_t expected = expectedPolyphony(normalized);

            // Every value in the sweep differs from the one before it (and from
            // the default 8), so each push MUST produce exactly one new call.
            REQUIRE(expected != engine.getPolyphony());

            fx.setParam(Seraphis::kPolyphonyId, normalized);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            ++expectedCalls;

            CHECK(engine.getPolyphony() == expected);
            CHECK(fx.proc->setPolyphonyCallCountForTest() == expectedCalls);

            // RE-PUSHING THE SAME VALUE MUST NOT RE-CALL setPolyphony.
            fx.setParam(Seraphis::kPolyphonyId, normalized);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            CHECK(engine.getPolyphony() == expected);
            CHECK(fx.proc->setPolyphonyCallCountForTest() == expectedCalls);
        }
        CHECK(fx.checkCanaries());
    }

    // =========================================================================
    // SC-019 clause 3d - A CORRUPT STREAM CONVERGES
    // =========================================================================
    // The change detector compares the stored value against the engine's CLAMPED
    // getPolyphony() (seraphis_engine.h:322, :665). Without clampPolyphony() at
    // the single conversion point (parameters/global_params.h:62) an out-of-range
    // stored value never equals the read-back, so the detector fires on EVERY
    // BLOCK FOREVER - re-arming sumGain_ (:349) and walking the allocator's
    // excess-slot loop (:339-348) once per block for the life of the instance.
    //
    // MEASURED SENSITIVITY, recorded so nobody over-reads this section: with the
    // shipped code there are TWO clamps between a hand-written stream and the
    // detector - loadGlobalParams() clamps on the way in
    // (parameters/global_params.h:176) and pushGlobalParams() clamps on the way
    // out - and handleGlobalParamChange() clamps the automation route too
    // (:86-88). There is therefore NO public API through which an out-of-range
    // polyphony can reach the stored atomic, and this section fails only if BOTH
    // clamps are removed. It is a genuine end-to-end convergence assertion, not a
    // single-clamp detector; the no-op-push failure mode is caught by clause 3c
    // above (verified: stubbing pushGlobalParams() fails 3c with 20 failed
    // assertions and leaves this section green).
    // =========================================================================
    SECTION("clause 3d: a corrupt polyphony in the stream converges after ONE push") {
        for (const Steinberg::int32 corrupt : {Steinberg::int32{0}, Steinberg::int32{20}}) {
            INFO("state stream polyphony = " << corrupt);

            SeraphisTest::ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

            Krate::DSP::SeraphisEngine& engine = engineOf(fx);
            const std::size_t before = fx.proc->setPolyphonyCallCountForTest();

            StreamPtr state = makeStateStream(corrupt);
            REQUIRE(fx.proc->setState(state.get()) == Steinberg::kResultOk);

            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            CHECK(engine.getPolyphony() >= std::size_t{1});
            CHECK(engine.getPolyphony() <= Krate::DSP::SeraphisEngine::kMaxVoices);

            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            CHECK(engine.getPolyphony() >= std::size_t{1});
            CHECK(engine.getPolyphony() <= Krate::DSP::SeraphisEngine::kMaxVoices);

            // TWO consecutive blocks, AT MOST ONE new setPolyphony call.
            const std::size_t after = fx.proc->setPolyphonyCallCountForTest();
            CHECK(after >= before);
            CHECK(after - before <= std::size_t{1});
            CHECK(fx.checkCanaries());
        }
    }

    // =========================================================================
    // SC-027 - THE SOFT LIMIT HAS A MEASURABLE EFFECT (FR-044)
    // =========================================================================
    SECTION("Seraphis_SoftLimitIsMeasurable") {
        const DriveRung rungs[] = {
            {"L0 1 note, vel 0.8, gain x1, poly 8, 2 s", kSingleNote, kSingleVelocity, 0.5,
             kPolyphonyNorm8, kTwoSeconds},
            {"L1 6 notes, vel 1.0, gain x2, poly 8, 2 s", kSixNoteChord, 1.0f, 1.0,
             kPolyphonyNorm8, kTwoSeconds},
            {"L2 16 notes, vel 1.0, gain x2, poly 16, 2 s", kSixteenNoteChord, 1.0f, 1.0,
             kPolyphonyNorm16, kTwoSeconds},
            {"L3 16 notes, vel 1.0, gain x2, poly 16, 4 s", kSixteenNoteChord, 1.0f, 1.0,
             kPolyphonyNorm16, kFourSeconds},
            {"L4 16 notes, vel 1.0, gain x2, poly 16, 16 s", kSixteenNoteChord, 1.0f, 1.0,
             kPolyphonyNorm16, kSixteenSeconds},
        };

        // The loudest rung's figures; the assertions below are made on it.
        double bestOffRms = 0.0;
        double bestOnRms = 0.0;
        float bestOffPeak = 0.0f;
        float bestMaxAbsDiff = 0.0f;
        double bestRelRms = 0.0;
        double bestRelRmsDrop = 0.0;
        const char* bestName = "";
        // Clause 3's rung: the loudest one whose pre-output peak is clear of the
        // limiter ceiling - see kSoftLimitDirectionCeilingFrac.
        float dirOffPeak = 0.0f;
        double dirRelRmsDrop = 0.0;
        const char* dirName = "";
        bool dirFound = false;

        for (const DriveRung& rung : rungs) {
            const Drive base{.masterGainNorm = rung.masterGainNorm,
                             .softLimitNorm = 0.0,
                             .polyphonyNorm = rung.polyphonyNorm,
                             .pitches = rung.pitches,
                             .velocity = rung.velocity,
                             .totalSamples = rung.totalSamples};
            Drive onDrive = base;
            onDrive.softLimitNorm = 1.0;

            const Render off = renderHeldChord(base);
            const Render on = renderHeldChord(onDrive);

            // THE PRE-OUTPUT-STAGE LEVEL. With the soft limit OFF the tape
            // saturator's blend is 0, so the OFF arm IS the pre-output-stage
            // signal up to a linear pre/de-emphasis + DC blocker and a limiter
            // that is exactly unity below its ceiling. Its RMS and peak are
            // therefore the level this rung was measured at.
            const double offRmsL = rmsOf(off.left);
            const double offRmsR = rmsOf(off.right);
            const double onRmsL = rmsOf(on.left);
            const double onRmsR = rmsOf(on.right);
            const double offRms = std::max(offRmsL, offRmsR);
            const double onRms = std::max(onRmsL, onRmsR);
            const float offPeak = std::max(maxAbs(off.left), maxAbs(off.right));
            const float diff =
                std::max(maxAbsDiff(on.left, off.left), maxAbsDiff(on.right, off.right));
            const double relRms =
                (offRms > 0.0)
                    ? std::max(rmsDiff(on.left, off.left), rmsDiff(on.right, off.right)) / offRms
                    : 0.0;

            // THE DIRECTED FORM of the same difference, and the WORSE of the two
            // channels. maxAbsDiff and rmsDiff are both SYMMETRIC in (on, off):
            // they are identical if the two arms are swapped, so neither can tell
            // FR-044's mapping from its inverse (on -> 0.0f, off -> 0.15f). This
            // one is signed - a saturator that ATTENUATES makes it positive - so
            // an inverted mapping drives it negative and fails.
            const double relRmsDrop =
                (offRmsL > 0.0 && offRmsR > 0.0)
                    ? std::min((offRmsL - onRmsL) / offRmsL, (offRmsR - onRmsR) / offRmsR)
                    : 0.0;

            // Every rung is REPORTED, not just the one the assertions use:
            // SC-027 requires the measured deltas at each level tried to be
            // recorded, so a failure is diagnosable without re-running.
            WARN("SC-027 " << rung.name << ": pre-output-stage rms=" << offRms
                           << " peak=" << offPeak << " (limiter ceiling " << kLimiterCeilingLin
                           << ") | maxAbsDiff(on,off)=" << diff << " (kSampleTolerance "
                           << Krate::DSP::TestUtils::kSampleTolerance
                           << ") | relative RMS difference=" << relRms
                           << " | on-arm rms=" << onRms
                           << " | worst-channel relative RMS drop=" << relRmsDrop);

            // Non-vacuity: this rung must not be comparing two silences.
            CHECK(offRms > 1.0e-6);

            if (offPeak <= (kSoftLimitDirectionCeilingFrac * kLimiterCeilingLin)
                && offPeak > dirOffPeak) {
                dirOffPeak = offPeak;
                dirRelRmsDrop = relRmsDrop;
                dirName = rung.name;
                dirFound = true;
            }

            if (diff > bestMaxAbsDiff) {
                bestMaxAbsDiff = diff;
                bestRelRms = relRms;
                bestRelRmsDrop = relRmsDrop;
                bestOffRms = offRms;
                bestOnRms = onRms;
                bestOffPeak = offPeak;
                bestName = rung.name;
            }
        }

        WARN("SC-027 loudest rung: " << bestName << " | pre-output-stage rms=" << bestOffRms
                                     << " peak=" << bestOffPeak
                                     << " | maxAbsDiff=" << bestMaxAbsDiff
                                     << " | relative RMS difference=" << bestRelRms
                                     << " | on-arm rms=" << bestOnRms
                                     << " | worst-channel relative RMS drop=" << bestRelRmsDrop);

        WARN("SC-027 direction rung: " << dirName << " | pre-output-stage peak="
                                       << dirOffPeak << " | worst-channel relative RMS drop="
                                       << dirRelRmsDrop);

        // Clause 1, AS SPECIFIED. No fallback floor, no tripwire - see the banner.
        REQUIRE(bestMaxAbsDiff > Krate::DSP::TestUtils::kSampleTolerance);

        // Clause 2 - the MEASURED relative-RMS floor.
        CHECK(bestRelRms > kSoftLimitRelRmsFloor);

        // FR-044's DIRECTION, which no symmetric metric above can assert: the ON
        // arm must be the ATTENUATED one, measured where the SATURATOR and not the
        // LIMITER decides the level (see kSoftLimitDirectionCeilingFrac and the
        // banner's note on L4's sign flip).
        REQUIRE(dirFound);
        CHECK(dirRelRmsDrop > kSoftLimitRelRmsDropFloor);
    }
}
