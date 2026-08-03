#pragma once

// ==============================================================================
// Seraphis - Audio Processor (audio thread)
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation.
// This header NEVER includes anything under controller/.
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"

// Phase 10: kMaxBlockSamples - the ONE block constant the send accumulator's
// capacity bound is stated against (seraphis_engine_config.h:40-43).
#include "engine/seraphis_engine_config.h"

#include "parameters/aether_params.h"
#include "parameters/atmosphere_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/effects_params.h"
#include "parameters/global_params.h"
#include "parameters/life_mod_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"

#include <krate/dsp/core/block_context.h>       // Phase 10 FR-030: SpectralDelay::process's 4th arg
#include <krate/dsp/core/crossfade_utils.h>     // Phase 10 C-5: equalPowerGains (the azimuth pan law)
#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/effects/spectral_delay.h>   // Phase 10 C-1 step 4: the send
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/brownian_drift.h>    // Phase 10 C-1 step 5: the wander sources
#include <krate/dsp/processors/midside_processor.h> // Phase 10 C-1 step 5: the global width stage
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <array>
#include <atomic>
#include <chrono>  // Phase 10 FR-041 clause 1: the effects-stage scoped timer
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Seraphis {

/// FR-042. The "no generation has ever been applied" sentinel. `voiceParamGeneration_`
/// starts at 0 and only ever increments, so SIZE_MAX can never compare equal to it
/// and the first process() call after construction always pushes.
inline constexpr std::size_t kGenerationSentinel = static_cast<std::size_t>(-1);

/// FR-047 (plan 3.4). ONE body, two situations - not two bodies. After a
/// re-prepare the DSP objects really were re-initialised and every surface must be
/// re-pushed unconditionally; after a preset load the atomics already carry the new
/// values and the ordinary compare-against-tracker path delivers whatever changed,
/// so forcing an UNCHANGED seed or polyphony through would be a gratuitous
/// discontinuity.
enum class SurfaceInvalidation { Reprepared, PresetLoad };

/// FR-047 (plan 3.4). `lastPushedPolyphony_`'s force-push sentinel. 0 is never a
/// legal clamped voice count - clampPolyphony() floors at 1 (global_params.h:75)
/// and SeraphisEngine::setPolyphony clamps to [1, kMaxVoices] - so it can never
/// compare equal to what the engine holds and the next pushGlobalParams() always
/// re-pushes.
inline constexpr std::size_t kPolyphonySentinel = 0;

/// FR-059 clause (b) item 2 (plan 3.5.2). The class-(b) smoothing times, fixed
/// HERE and nowhere else. FR-059(b) clause 2 offers "a single value shared by
/// all class-(b) IDs OR a per-ID column of kContinuityMechanism[]"; SC-005's
/// measured result forces the SECOND form, and this is the two-value column that
/// implements it. `kContinuityMechanism[]` carries the same numbers per row and
/// a gate asserting each class-(b) row names one of exactly these two.
///
/// WHY TWO NUMBERS, MEASURED NOT ASSUMED. The mechanism delivers on the ABSOLUTE
/// 64-sample control-chunk grid, so the value the consumer sees is a STAIRCASE
/// whose first stair is `1 - e^(-64/tau_samples)` of the whole step. Whether
/// that stair is audible depends entirely on what the consumer does with it, and
/// the two families differ by an order of magnitude:
///
///   - BODY COEFFICIENTS (801, 802). resonance_ / damping_ reach b1_eff / b3_eff
///     and T60. continuous_body.h:2545-2558 states in terms that a step there
///     "changes a decay slope ... neither is a discontinuity in the output,
///     because sinState_/cosState_ carry through untouched", and the one gain
///     path (engineDriveFor -> slot.driveLog10) is itself smoothed at
///     kDriveSmoothMs = 50 ms. MEASURED on SC-005's own render: 801 scores
///     1.044 x smoothed and 1.045 x with FR-059a's probe SNAPPING it; 802 scores
///     1.167 x both ways. The smoother is retained (the values genuinely are
///     stored raw, which is the class-(b) test), but 20 ms is already far below
///     the floor and nothing is gained by lengthening it.
///
///   - AETHER DEPTHS (1215, 1216) AND THE FIVE MACROS. sizeBreathDepth_ scales a
///     live [-1,+1] modulator that is added to the SMOOTHED Size BEFORE the
///     EXPONENTIAL S(v) mapping (aether_reverb.h:3036-3055), and the product
///     becomes effectiveDelay_[i] - the delay-line READ LENGTH, consumed raw at
///     :4256. A stair in the depth is therefore a jump of the read pointer by
///     MANY samples, and the resulting discontinuity SATURATES: it stops
///     shrinking in proportion to the stair as soon as the jump exceeds ~1
///     sample. MEASURED (SC-005's own render, ID 1215, ratio against its own
///     reference): 20 ms -> 1.817, 60 ms -> 2.297, 100 ms -> 1.847,
///     200 ms -> 1.172, 300 ms -> 1.126, 500 ms -> 1.093. The statistic is
///     IDENTICAL at 300 ms and 500 ms (0.003258 vs 0.003257), i.e. 300 ms is
///     where the stair disappears under the render's own floor and no further
///     lengthening buys anything.
///
///     300 ms IS NOT A FITTED NUMBER: it is exactly
///     `AetherReverb::kSizeSmoothingMs = 300.0f` (aether_reverb.h:2731), the
///     component's own smoothing time for the very quantity these depths
///     modulate. The rule it instantiates - a class-(b) time constant must be at
///     least the component's own smoothing time for the quantity it modulates -
///     is what makes ID 1215 provably equivalent to ID 1201 (Size), the class-(a)
///     ID that rides that smoother and passes SC-005.
///
///     The five macros take the same number because they reach
///     AetherSizeBreathDepth (seraphis_macro_matrix.h kRows), so the slowest
///     target they touch sets their floor.
///
/// OnePoleSmoother's argument is TIME-TO-99 % (primitives/smoother.h:86-91,
/// :158-160), so tau = ms/5000 s. Derived per family, for D = 1.0 (every
/// class-(b) ID is a [0,1] span) and kCompletionThreshold = 1.0e-4f
/// (smoother.h:55), on the 1.3333 ms chunk / 10.6667 ms block grid at 48 kHz:
///
///   body   (20 ms):  tau =  4 ms, stair 0.2835, t =  36.84 ms,
///                    N_chunk =  28, N_block =  4
///   aether (300 ms): tau = 60 ms, stair 0.0220, t = 552.62 ms,
///                    N_chunk = 415, N_block = 52
///
/// The remedy rule stays ONE-DIRECTIONAL: if SC-005 ever finds a step, LENGTHEN
/// the owning constant and re-measure. Loosening SC-005's bound is not a remedy,
/// and neither is deleting the control.
inline constexpr float kParamSmoothMs = 20.0f;
inline constexpr float kAetherDepthSmoothMs = 300.0f;

// ==============================================================================
// Phase 10 - FR-015 / FR-024a. EVERY effects constant, named HERE with the header
// line that justifies it, so no use site ever carries a bare literal.
// ==============================================================================

/// C-2 clause 5 / FR-003a. SpectralDelay::process MUST be called with EXACTLY
/// this many samples and never with a MIDI-slice length: its output stream
/// position depends on how many analysis frames happened to be ready, so the same
/// audio delivered as one 2048-sample call and as four 512-sample calls comes
/// back a whole hop apart - permanently. This IS the component's hop at
/// kDefaultFFTSize = 1024 (`hopSize_ = fftSize_ / 2`, spectral_delay.h:850, :89).
inline constexpr std::size_t kFxSendChunkSamples =
    Krate::DSP::SpectralDelay::kDefaultFFTSize / 2u;  // 512

/// FR-008 / FR-009. The send's return-gain ramp. Identical BY CONSTRUCTION to
/// kParamSmoothMs above - FR-038b clause 2: ID 1410 gets ONE smoother, not a
/// class-(b) smoother plus a second private engage ramp that would fight it.
inline constexpr float kFxReturnRampMs = kParamSmoothMs;  // 20 ms
static_assert(kFxReturnRampMs == kParamSmoothMs, "FR-038b cl.2: ONE smoother for ID 1410");

/// FR-009a. The drain window, in ms: ONE kMaxDelayMs (spectral_delay.h:92), the
/// longest delay the send can be holding when it is bypassed.
inline constexpr float kFxSendDrainMs = Krate::DSP::SpectralDelay::kMaxDelayMs;  // 2000

/// FR-009a. Linear peak below which the drain ends EARLY. This is what bounds a
/// bypass excursion's cost by ENERGY rather than by wall clock.
inline constexpr float kFxSendDrainFloor = 1.0e-6f;

/// C-4 / FR-023a. The return gain kFxSpectralFreezeId forces while it is
/// engaged, so a freeze at kFxDelayMixId = 0 is audible rather than a silent
/// no-op the user cannot distinguish from a broken control.
inline constexpr float kFxFreezeMinReturnGain = 0.5f;

/// Plan D-5. Live post-Aether samples the send must have consumed before
/// setFreezeEnabled(true) is pushed, so the frame processSpectralFrame captures
/// (spectral_delay.h:677-688) is assembled entirely from live bus rather than
/// from pre-bypass residue. 2 x kDefaultFFTSize = four hops = two analyses on
/// wholly-live frames.
inline constexpr std::uint64_t kFxFreezePrimeSamples =
    2u * Krate::DSP::SpectralDelay::kDefaultFFTSize;  // 2048 = 42.7 ms @ 48 kHz

/// C-5. Width span in percent per unit depth-scaled drift. 50 rather than 100
/// because BrownianDrift is bipolar with kInternalStd = 0.5, so at depth 1 the
/// walk normally stays inside +/-0.5: width lands typically 75-125 %, extremes
/// 50-150 %, and can never collapse toward mono
/// (midside_processor.h:65-67 - kMinWidth = 0 IS a mono collapse).
inline constexpr float kWanderWidthSpanPercent = 50.0f;

/// Plan D-4. UNITY AT CENTRE. equalPowerGains is a CROSSFADE law
/// (crossfade_utils.h:50-53): it preserves energy across two DIFFERENT signals
/// that are then summed. Applied to the two channels of ONE stereo bus the
/// constant quantity is gL^2 + gR^2, so the raw law's cos(pi/4) on both channels
/// drops the whole bus -3.01 dB the instant kFxAzimuthDepthId leaves 0 - a
/// steady-state level STEP as a function of a depth control, which no smoother
/// removes because it is not a transient. With this compensation gL^2 + gR^2 = 2
/// at every position (still position-independent, which is what SC-006's
/// argument needs) and centre is exactly unity per channel, so FR-010's skip
/// boundary is continuous. Peak per-channel gain at full deflection is +3.01 dB,
/// bounded by the limiter.
inline constexpr float kFxAzimuthCentreComp = 1.41421356f;  // sqrt(2) = 1 / cos(pi/4)

/// FR-010 / FR-010a. How far from unity the azimuth pair may sit and still count
/// as IDENTITY for wanderAtIdentity()'s purposes.
///
/// IT IS NOT ZERO, AND THE REASON IS MEASURED, NOT STYLISTIC. D-4's claim that
/// "centre is exactly unity per channel" is exact in REAL arithmetic and one ulp
/// short in IEEE-754 binary32: equalPowerGains(0.5f) is cos(kHalfPi/2) which
/// rounds to 0x3F3504F3 = 0.70710677f, and 0.70710677f * kFxAzimuthCentreComp
/// rounds to 0.99999994f - exactly one ulp below 1.0f. An `== 1.0f` identity test
/// could therefore NEVER be satisfied by the pair the control loop actually
/// produces, FR-010's mandatory skip would never re-engage after a single wander
/// excursion, and the stage would run forever at a cost C-3 says it must not pay.
/// 1e-6f is ~17 ulp at unity - wide enough to absorb a cosf/sinf that differs by
/// an ulp between MSVC, glibc and Apple libm, and ~0.00001 dB, i.e. four orders
/// below any audible gain step, so the sub-ulp discontinuity the skip introduces
/// at that boundary cannot be heard or measured by SC-008's per-sample statistic.
inline constexpr float kFxAzimuthIdentityEps = 1.0e-6f;

/// C-5 / FR-024. The wander's control grid. NOT a new number: it is the shared
/// control clock SeraphisEngine (seraphis_engine.h:213) and AetherReverb already
/// run on, and the same grid the slice loop aligns sub-slices to while any
/// class-(b) smoother is unsettled (processor.cpp's D-6 clamp).
inline constexpr std::size_t kWanderControlChunkSamples =
    Krate::DSP::SeraphisEngine::kControlChunkSamples;  // 64

/// C-5 / FR-026. TWO DISTINCT salts. Identical salts would make width and
/// azimuth walk in lockstep off the one seed, which reads as a single moving
/// object rather than two independent ones.
inline constexpr std::uint32_t kFxWidthDriftSalt = 0x5E11A001u;
inline constexpr std::uint32_t kFxAzimuthDriftSalt = 0x5E11A002u;
static_assert(kFxWidthDriftSalt != kFxAzimuthDriftSalt,
              "C-5: the two drift salts must differ");

/// Plan section 3.1's capacity bound. A power of two so the ring index is a
/// mask, and at least one chunk plus one whole slice, which is the most that can
/// be resident between two chunk runs.
inline constexpr std::size_t kFxFifoCapacity = 4096;
static_assert((kFxFifoCapacity & (kFxFifoCapacity - 1u)) == 0u, "the ring index is a mask");
static_assert(kFxFifoCapacity >= kFxSendChunkSamples + kMaxBlockSamples,
              "plan sec. 3.1 bound: one chunk plus one whole slice must fit");

namespace detail {
/// FR-059a. SC-005 positive control (b). DECLARED HERE, DEFINED ONLY BY THE
/// SC-005 TEST TU (tests/integration/param_continuity_test.cpp).
///
/// Shape: Krate::DSP::detail::SeraphisVoiceSilenceRampProbe - but declared
/// PLUGIN-SIDE on purpose, because adding a bypass seam to dsp/ is what FR-071
/// forbids. Its SOLE capability is to set Processor::paramSmootherBypass_,
/// which makes advanceParamSmoothers() snap instead of ramp - a deliberate
/// un-smoothed write. The library never defines it, so a shipping build has no
/// way to call it and the branch it drives is false on every shipping path.
///
/// ODR swept this session:
/// `grep -rn "SeraphisParamSmootherBypassProbe" dsp/ plugins/` -> 0 hits.
struct SeraphisParamSmootherBypassProbe;

/// Phase 10 FR-040 (as amended by plan D-7). DECLARED HERE, DEFINED ONLY BY THE
/// PHASE 10 EFFECTS TU (tests/integration/effects_chain_test.cpp).
///
/// Same shape and same rationale as the Phase 9 probe above - plugin-side, never
/// under dsp/ - but it carries THREE capabilities, not one, because three of this
/// phase's success criteria each MANDATE one of them and an implementer working
/// from FR-040's earlier "sole capability" wording could not write two of them:
///
///   1. Processor::effectsStageBypassed_    - renderSlice() skips C-1 steps 4
///      and 5 at runtime. SC-002's negative control and SC-012 are measured
///      against it (both are SAME-BINARY runtime comparisons: FR-039 forbids a
///      second test executable, so a #if-ed second variant of this file cannot
///      exist).
///   2. Processor::effectsStageAfterOutput_ - runs C-1 step 5 AFTER step 6.
///      SC-003(a)'s MANDATORY positive control: the limiter-last invariant is
///      only proven if the deliberately-wrong order provably fails.
///   3. Processor::effectsReturnRampSnap_   - snaps the FR-008/FR-009 20 ms
///      return-gain ramp to instant. SC-008's MANDATORY positive control (b).
///      Consumed by the send stage's engage ramp (T017); it is a flag on that
///      branch, never a branch of its own.
///
/// The library never defines the probe, so a shipping build has no way to call
/// it and all three branches are false on every shipping path.
///
/// ODR swept this session:
/// `grep -rn "SeraphisEffectsStageBypassProbe" dsp/ plugins/` -> 0 hits.
struct SeraphisEffectsStageBypassProbe;
}  // namespace detail

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // -------------------------------------------------------------------------
    // Test-only read surfaces (NEVER called from process()).
    // -------------------------------------------------------------------------
    [[nodiscard]] Krate::DSP::SeraphisEngine* engineForTest() noexcept { return engine_.get(); }
    [[nodiscard]] Krate::DSP::AetherReverb* reverbForTest() noexcept { return reverb_.get(); }
    [[nodiscard]] std::size_t setPolyphonyCallCountForTest() const noexcept {
        return setPolyphonyCalls_;
    }

    // --- FR-041a (plan 3.1). Phase 9 push-cadence seams. -----------------------
    // Plain std::size_t counters written only from the audio thread and read only
    // from the test thread once the render has completed; no atomic is needed.
    /// SC-007: SUCCESSFUL applications of the 37-field voice broadcast.
    [[nodiscard]] std::size_t applyVoiceParamsCallCountForTest() const noexcept {
        return applyVoiceParamsCalls_;
    }
    /// SC-007: spectral fan-outs that CLEARED the pending flag (successes only).
    [[nodiscard]] std::size_t applySpectralStatesCallCountForTest() const noexcept {
        return applySpectralStatesCalls_;
    }
    /// SC-007: fan-out ATTEMPTS, including retries that did not clear. Without
    /// this, a retry that re-ran every block for the length of a held note is
    /// invisible (the success counter never moves).
    [[nodiscard]] std::size_t applySpectralStatesAttemptCountForTest() const noexcept {
        return applySpectralStatesAttempts_;
    }
    /// SC-007: INVOCATIONS of the ten-control Aether push.
    [[nodiscard]] std::size_t applyAetherParamsCallCountForTest() const noexcept {
        return applyAetherParamsCalls_;
    }
    /// SC-007: setTargetBase INVOCATIONS, so a per-slice re-push is visible.
    [[nodiscard]] std::size_t setTargetBasePushCountForTest() const noexcept {
        return setTargetBasePushes_;
    }
    /// SC-003's `MB` SECONDARY - the only route to getTargetBase().
    [[nodiscard]] const Krate::DSP::SeraphisMacroMatrix& macroMatrixForTest() const noexcept {
        return macros_;
    }
    /// SC-013 / SC-003's `CFG` acceptance clause.
    [[nodiscard]] bool spectralStatesPendingForTest() const noexcept {
        return spectralStatesPending_;
    }
    /// SC-012. Out-of-range slots return slot 0 rather than reading past the end.
    [[nodiscard]] const Krate::DSP::SpectralState& spectralSlotForTest(int slot) const noexcept {
        const int s = (slot < 0 || slot >= 4) ? 0 : slot;
        return spectralSlots_[static_cast<std::size_t>(s)];
    }
    /// SC-023 clause 5: the staging index was consumed on the AUDIO thread, once.
    [[nodiscard]] std::size_t spectralHandoffConsumeCountForTest() const noexcept {
        return spectralHandoffConsumes_;
    }
    /// SC-010's LOCALIZED [morph]-block cursor check needs a [morph] block
    /// written by exactly the path getState() uses.
    [[nodiscard]] const MorphParams& morphParamsForTest() const noexcept { return morphParams_; }

    /// SC-023 clause 6 / clause 7(d) NEGATIVE-CONTROL SEAM.
    ///
    /// plan 7.13 words both clauses as "a compile-time test-TU switch", which is
    /// not available here: processor.cpp is compiled ONCE into seraphis_tests
    /// (tests/CMakeLists.txt:30), so a macro defined by the test TU cannot reach
    /// this file's definitions. The seam is therefore a pair of file-static
    /// booleans, default false, set only by this function and read only by
    /// setState() (preset arm) and setupProcessing() (re-prepare arm). No
    /// shipping path calls it - the plugin binary never links a caller - and the
    /// test wraps it in an RAII guard so a failed assertion cannot leave either
    /// arm disabled for the rest of the suite.
    static void setSurfacePushDisabledForTest(bool onPresetLoad, bool onReprepare) noexcept;
    // FR-045's four ENG cadence counters. Nothing else can see their cadence:
    // SeraphisEngine::setSeed is deterministic per call, so a redundant re-push is
    // invisible in a render, and SC-008's steady-state arm does not time this path.
    [[nodiscard]] std::size_t engSeedPushCountForTest() const noexcept { return engSeedPushes_; }
    /// A NAMED ALIAS of setPolyphonyCallCountForTest(), not a second counter - so
    /// no Phase 8 assertion moves.
    [[nodiscard]] std::size_t engPolyphonyPushCountForTest() const noexcept {
        return setPolyphonyCalls_;
    }
    [[nodiscard]] std::size_t engSoftLimitPushCountForTest() const noexcept {
        return engSoftLimitPushes_;
    }
    [[nodiscard]] std::size_t engFreezePushCountForTest() const noexcept {
        return engFreezePushes_;
    }

    // --- Phase 10 FR-041 (plan D-8). SEVEN test-only read surfaces, plus one
    //     truncation flag. Same discipline as the Phase 9 block above: plain
    //     scalars written only from the audio thread and read only once the
    //     render has completed, so no atomic is needed.
    /// Clause 1. Accumulated wall time of C-1 steps 4-5 (and the tap copy that is
    /// deliberately inside the same scope), summed PER SLICE. SC-012/SC-013
    /// divide it by the per-CALL divisor below.
    [[nodiscard]] double effectsStageNsForTest() const noexcept { return effectsStageNs_; }
    /// Clause 1's DIVISOR, and the reason it is named "process calls": it is
    /// incremented exactly ONCE PER process() CALL, never once per renderSlice().
    /// SC-012 and SC-013 compare against PER-BLOCK budgets, but the slice loop
    /// subdivides on every MIDI event, on the 2048 cap and - while any class-(b)
    /// smoother is unsettled - on the absolute 64-sample grid, so a per-slice
    /// divisor under-reports by up to 8x and makes SC-013's budget structurally
    /// unable to fail.
    [[nodiscard]] std::size_t effectsStageProcessCallsForTest() const noexcept {
        return effectsStageProcessCalls_;
    }
    /// Clause 2 - FR-008's deferred SpectralDelay::reset().
    [[nodiscard]] std::size_t spectralDelayResetCountForTest() const noexcept {
        return spectralDelayResets_;
    }
    /// Clause 3 - FR-022/FR-024 pushes actually issued.
    [[nodiscard]] std::size_t effectsPushCountForTest() const noexcept { return effectsPushes_; }
    /// Clause 4 - FR-011's per-block BrownianDrift::processBlock cadence.
    [[nodiscard]] std::size_t widthDriftBlockCountForTest() const noexcept {
        return widthDriftBlocks_;
    }
    /// Clause 5 - FR-012's bypass-predicate evaluations.
    [[nodiscard]] std::size_t bypassPredicateEvalCountForTest() const noexcept {
        return bypassPredicateEvals_;
    }
    /// Clause 7 - one increment per SpectralDelay::process() call. THE ONLY
    /// CI-GATED OBSERVATION OF FR-007: SC-012's threshold is [.perf]-tagged and
    /// therefore outside the gate, and SC-002 is structurally blind to a running
    /// send (at mix 0 the mix loop adds `fxOut[i] * 0.0f`, so the bus stays
    /// bit-identical).
    [[nodiscard]] std::size_t sendChunkCountForTest() const noexcept { return sendChunks_; }
    /// Clause 6 - THE PRE-OUTPUT-STAGE TAP. A copy of the stereo bus taken
    /// immediately before engine_->processOutputStage() (C-1 step 6), so every
    /// isolated-return measurement (SC-003(b) onward, SC-005, SC-007, SC-011a,
    /// SC-019) is clear of TapeSaturator + TruePeakLimiter, which are nonlinear
    /// in the quantity those criteria measure. SC-003(a) is the one deliberate
    /// carve-out: it measures the TRUE plugin output, because the limiter is its
    /// subject, and uses the tap only to establish its precondition.
    ///
    /// The LENGTH IS CARRIED BY THE SPAN and is the number of samples this
    /// process() call actually taped - see preOutputTapTruncatedForTest().
    [[nodiscard]] std::span<const float> preOutputTapLForTest() const noexcept {
        return std::span<const float>(preOutTapL_.data(), preOutTapSize_);
    }
    [[nodiscard]] std::span<const float> preOutputTapRForTest() const noexcept {
        return std::span<const float>(preOutTapR_.data(), preOutTapSize_);
    }
    /// TRUE when the most recent process() call delivered more samples than the
    /// tap buffers hold. The buffers are pinned to
    /// SeraphisEngine::kMaxBlockSamples = 2048 (FR-041 clause 6) while the
    /// processor explicitly supports larger host blocks, so without this flag a
    /// 4096-sample block silently yields a half-length tap and every tap-based
    /// criterion measures half a render with NO error signal. Every criterion
    /// that reads the tap must assert this is false for its render.
    [[nodiscard]] bool preOutputTapTruncatedForTest() const noexcept {
        return preOutTapTruncated_;
    }

    /// THE INSTRUMENTATION MASTER SWITCH for clause 1's scoped timer and
    /// clause 6's tap - default FALSE, so NO SHIPPING PATH EVER RUNS EITHER.
    ///
    /// WHY THIS EXISTS. Both are, in FR-041's own words, "test-only": clause 1
    /// is a measurement surface and clause 6 is "a test-only copy of the stereo
    /// bus". Left unconditional they were neither. renderSlice() is called ONCE
    /// PER SLICE, and the slice loop subdivides on every MIDI event, on the 2048
    /// cap and - while any class-(b) smoother is unsettled - on the absolute
    /// 64-sample control grid (processor.cpp:1298-1301), so a 2048-sample host
    /// block arriving during ANY parameter ramp ran 32 slices = 64
    /// std::chrono::steady_clock::now() reads plus 32 full-bus copies, on the
    /// audio thread, in the shipped plugin. Constitution II forbids system calls
    /// on the audio thread outright, and the clock read is one on every platform
    /// this plugin targets; Membrum's comparable timer is per process() CALL
    /// (membrum/src/processor/processor.cpp:641), not per slice.
    ///
    /// WHAT THE SHIPPING PATH PAYS NOW: one predictable, always-false branch per
    /// slice on a member already in cache. No clock read, no copy.
    ///
    /// This is a MUTATOR, not one of FR-041's seven read surfaces - that set is
    /// closed and stays closed. It follows the precedent of
    /// setSurfacePushDisabledForTest() above, except that it is per-INSTANCE
    /// rather than file-static, so no test can leak it into another's render.
    /// SC-012/SC-013 are unaffected: the shared ProcessorFixture::prepare()
    /// turns it on (tests/seraphis_test_fixture.h), so every measured figure
    /// still charges the tap copy to the scoped timer exactly as before.
    void setEffectsStageInstrumentedForTest(bool on) noexcept {
        effectsStageInstrumented_ = on;
    }

private:
    // FR-059a. The ONE seam SC-005's positive control (b) needs. Declared above
    // this class and defined ONLY by the SC-005 test TU, so no shipping build
    // can name it, let alone call it.
    friend struct detail::SeraphisParamSmootherBypassProbe;
    // FR-040 (plan D-7). The three seams SC-002, SC-003(a)'s positive control,
    // SC-008's positive control (b) and SC-012 need. Declared above this class
    // and defined ONLY by the Phase 10 effects test TU, so no shipping build can
    // name it, let alone call it.
    friend struct detail::SeraphisEffectsStageBypassProbe;

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushGlobalParams() noexcept;                                    // FR-024 step 0 / FR-024a
    void renderSlice(float* outL, float* outR, std::size_t n) noexcept;  // FR-024 steps 2-6

    /// Phase 10 C-1 STEP 4 - the spectral-delay SEND. Called from renderSlice()
    /// on the post-master-gain bus, IN PLACE, between the master-gain loop and
    /// the pre-output tap, and ONLY when fxSendRuns_ is true (FR-007's exact
    /// prohibition predicate: while the send is neither active nor draining this
    /// function is not called at all, so there is no SpectralDelay::process, no
    /// copy of the bus into fxIn*, and no read or write of any send buffer).
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free. Every
    ///      buffer was sized once in setupProcessing() and is indexed through
    ///      .data() / operator[], never .at().
    void runSendStage(float* busL, float* busR, std::size_t n) noexcept;

    /// Phase 10 plan section 3.3 - the send's THREE-STATE machine (FR-007,
    /// FR-008, FR-009, FR-009a, FR-023a). Called exactly ONCE per process()
    /// call from the pre-slice block (FR-012), never per slice, and it is the
    /// sole writer of fxSendRuns_ and fxEffectiveReturnGain_.
    ///
    /// @param blockSamples The whole process() call's sample count - NOT a slice
    ///        length. Every counter this function advances is a wall-clock
    ///        quantity (the bypass age, the drain countdown, the freeze priming
    ///        window), so advancing them per slice would make them depend on how
    ///        the host and the MIDI-slice loop happened to partition the block.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    void updateEffectsBypassState(std::size_t blockSamples) noexcept;

    /// Phase 10 C-1 STEP 5 - stereo wandering (MidSideProcessor width, then the
    /// azimuth pan), IN PLACE on the same bus and ALWAYS BEFORE step 6: a width
    /// or azimuth change applied after the limiter re-inflates peaks above the
    /// ceiling (processor.cpp's master-gain banner records the same prohibition
    /// for a post-limiter multiply). Its two BrownianDrift sources are prepared
    /// in setupProcessing() and advanced HERE on the absolute 64-sample grid
    /// while the stage runs, and by the pre-slice block while it is skipped -
    /// FR-011 requires them to run every block regardless of any bypass state,
    /// and SC-017 requires the value the controls are computed from to be a
    /// function of the absolute sample position rather than of the host's block
    /// length.
    ///
    /// The body is INTERLEAVED over the absolute 64-sample control grid (plan
    /// R-14): each sub-chunk computes its controls and then processes ITS OWN
    /// samples. A control loop run to completion before one audio call delivers
    /// nothing - setWidth() and setTarget() only move targets.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    void runWanderStage(float* busL, float* busR, std::size_t n) noexcept;

    /// FR-010a's DISENGAGE arm (plan section 3.4). TRUE only when every part of
    /// the wander stage has actually reached exact identity, so the stage can be
    /// skipped without stepping the stereo image in one sample.
    ///
    /// MidSideProcessor exposes only getWidth() - the TARGET, not its internal
    /// widthSmoother_'s progress - and the Non-goals forbid adding a dsp/
    /// accessor, so the component's own kDefaultSmoothingMs closes the gap
    /// through the fxWanderSettleRemaining_ countdown rather than through a new
    /// getter.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    [[nodiscard]] bool wanderAtIdentity() const noexcept;

    /// Phase 10, plan section 3.1. THE ONE DEFINITION of the send accumulator's
    /// start state, with exactly three call sites: setupProcessing(),
    /// setActive(false), and the single deferred mid-render site at the top of
    /// runSendStage(). It zeroes both FIFOs AND RESTORES THE ONE-CHUNK PRE-FILL
    /// on the output side - zeroing the output counters instead would break the
    /// section 3.1 invariant and wrap a std::size_t on the first read.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free. Four
    ///      std::fill over vectors sized once in setupProcessing().
    void clearFifos() noexcept;

    // --- Phase 9 (plan 3.2, 3.3, 3.4, 3.5.5, 3.6) ----------------------------
    /// FR-042. The route classification lives HERE and nowhere else, so C-6's
    /// routing cannot be restated (and desynchronised) at three call sites.
    void markDirty(Steinberg::Vst::ParamID id) noexcept;
    /// FR-041b. IDs 409-412. Copies 540 B out of factoryStates_ - it must NEVER
    /// call makeFactoryState(), which is ~200 std::pow/std::exp per call.
    void refreshSpectralSlotFromFactory(Steinberg::Vst::ParamID id) noexcept;
    /// FR-002. The 37 VP fields, gathered from the packs.
    [[nodiscard]] Krate::DSP::SeraphisVoiceParams buildVoiceParams() const noexcept;
    /// FR-003. The single mapping from a macro target to its owning atomic.
    [[nodiscard]] float baseValueForTarget(
        Krate::DSP::SeraphisMacroTarget target) const noexcept;
    void pushVoiceParams() noexcept;             // FR-042
    void pushMacroSurfaces() noexcept;           // FR-043
    void pushAetherParamsIfDirty() noexcept;     // FR-044
    /// Phase 10 D-2 / FR-021 (plan 2.5.4). Called ONCE per process() call from the
    /// pre-slice block, beside pushAetherParamsIfDirty(). It is the SOLE writer on
    /// SeraphisEngine::setOutputSaturation on the audio thread: ID 2 (kSoftLimitId)
    /// keeps its shipped meaning as a GATE and ID 1400 (kFxSaturationId) supplies
    /// the amount that gate passes, so the two can never fight over one setter.
    ///
    /// It is ALSO (T014) the sole push site for the SpectralDelay surface
    /// (IDs 1411-1419, FR-022), the send/drift seed burst (FR-026, FR-027) and the
    /// shared wander rate (ID 1442, FR-025) - every one of them ON CHANGE ONLY
    /// against its own tracker. IDs 1410, 1441 and 1443 are NOT pushed here (they
    /// are the three class-(b) smoothers, targeted in setParamSmootherTargets()),
    /// nor is 1440 (pushed on the wander stage's 64-sample control grid).
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    void pushEffectsParams() noexcept;
    void pushSpectralStatesIfPending() noexcept; // FR-046
    void consumeSpectralSlotHandoff() noexcept;  // FR-041b (plan 3.7 steps 3-4)
    void updateSyncedTravelRate(const Steinberg::Vst::ProcessContext* ctx) noexcept;  // FR-056
    void pushAllSurfaces(SurfaceInvalidation scope) noexcept;                         // FR-047
    /// FR-047. The ONLY thing setState() writes toward the audio thread: a
    /// single release store. pushAllSurfaces()' body runs from process().
    void requestPushAllSurfaces() noexcept;
    /// Plan 3.7 step 1. The first of {cursor, cursor+1, cursor+2} mod 3 that is
    /// neither published nor being consumed. Message thread only.
    [[nodiscard]] std::size_t pickStagingBuffer() noexcept;
    // --- FR-059 clause (b): the class-(b) smoother machinery (plan 3.5) -------
    /// Twelve setTarget() calls (smoother.h:170) from the already-clamped
    /// atomics - EXCEPT ID 1410, whose target is fxEffectiveReturnGain_, the
    /// value updateEffectsBypassState() composed for this process() call, and
    /// not the raw kFxDelayMixId atomic (FR-023a lifts it to
    /// kFxFreezeMinReturnGain while the freeze is engaged).
    /// Called ONCE per process(), from the plan 3.3 pre-slice block, BEFORE the
    /// slice loop reads anyClassBSmootherUnsettled(). It deliberately does NOT
    /// consume snapParamSmoothers_ - advanceParamSmoothers() does, because the
    /// snap has to outlive the target set that precedes it.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    void setParamSmootherTargets() noexcept;

    /// Advance (or snap) all twelve by the SUB-SLICE's own sample count, so the
    /// ramp is wall-clock correct whatever the host does. advanceSamples() is
    /// the O(1) closed form (smoother.h:243-256), so advancing by n once equals
    /// advancing by n/2 twice up to float rounding.
    ///
    /// IT DOES NOT SET TARGETS. Calling setParamSmootherTargets() from here
    /// would make the slice loop's predicate read a STALE target on the first
    /// slice after every change - every class-(b) smoother would still have
    /// current_ == target_, isComplete() would be true, no subdivision would
    /// happen and 93.0 % of the step would land in one push at block 512 (plan
    /// 3.5.4). That collapses SC-005's positive control (b) from 3.53 x to
    /// ~1.075 x against a 1.5 x bound, i.e. structurally incapable of failing.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    void advanceParamSmoothers(std::size_t sliceSamples) noexcept;

    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    ///      Returns a FIXED-SIZE array BY VALUE - TWELVE raw pointers to members
    ///      of `*this`, one per class-(b) ID: Phase 9's nine (plan 3.5.3) plus
    ///      Phase 10's three (FR-038b clause 2). The return type is
    ///      PINNED here because this is the hottest new audio-thread path in
    ///      the phase (twice per sub-slice, up to 32 times per 2048-sample
    ///      block while settling) and a range-returning helper with an unstated
    ///      type is where an implementer would reach for std::vector and
    ///      allocate per sub-slice. SC-006 would catch that, but only after the
    ///      fact and only if the settling window overlaps the measured render.
    [[nodiscard]] std::array<Krate::DSP::OnePoleSmoother*, 12> classBSmoothers() noexcept;

    /// The ONLY class-(b) `VP` row (ID 801). Drives pushVoiceParams()' settling
    /// clause.
    [[nodiscard]] bool anyVoiceClassBSmootherUnsettled() const noexcept;
    /// The five macro knobs (IDs 100-104). Owned by pushMacroSurfaces()' macro
    /// half, NOT by its 27-base loop.
    [[nodiscard]] bool anyMacroSmootherUnsettled() const noexcept;
    /// PER-TARGET, never one flag for all 27 (plan 3.5.5). A single global flag
    /// would push all 27 bases on every chunk of a settling window - up to 756
    /// increments of setTargetBasePushes_ - and falsify SC-007's own table.
    [[nodiscard]] bool targetClassBUnsettled(
        Krate::DSP::SeraphisMacroTarget target) const noexcept;
    /// Drives the 64-sample SUBDIVISION only. When this is false the slice
    /// structure is exactly Phase 8's and there is no extra cost.
    [[nodiscard]] bool anyClassBSmootherUnsettled() const noexcept;

    // NO announceLatencyIfChanged(), NO lastReportedLatency_. Plan §1.3 C-1/C-2:
    // the reported latency is the CONSTANT 1024 in every reachable state
    // (AetherReverb::getLatencySamples() returns spectralEnabled_ ?
    // diffusionFftSize_ : 0, already 1024 before any prepare), so there is no
    // transition to announce; and a Steinberg::Vst::AudioEffect has NO route to
    // an IComponentHandler - the handler is delivered only to the edit
    // controller. The FUnknownPtr<IComponentHandler>(getHostContext())
    // substitute is FORBIDDEN: that FUnknown* is an IHostApplication and the
    // query is null in every real host. If a later phase makes the latency
    // variable, add it as processor -> IMessage -> controller ->
    // getComponentHandler()->restartComponent(kLatencyChanged).

    // FR-022: NEVER by value. sizeof(SeraphisEngine) is 771 968 B against MSVC's
    // 1 MiB default stack (seraphis_engine.h:119-122, :159-164).
    std::unique_ptr<Krate::DSP::SeraphisEngine> engine_;
    std::unique_ptr<Krate::DSP::AetherReverb> reverb_;
    Krate::DSP::SeraphisMacroMatrix macros_{};  // ~20 B; by value is fine

    GlobalParams globalParams_{};
    MacroParams macroParams_{};

    // --- FR-041: one instance of each Phase 9 pack, BY VALUE ------------------
    CloudParams cloudParams_{};
    MorphParams morphParams_{};
    LifeModParams lifeParams_{};
    BodyParams bodyParams_{};
    AtmosphereParams atmosParams_{};
    AetherParams aetherParams_{};

    // --- Phase 10 FR-013 / FR-018: the effects pack, BY VALUE -----------------
    EffectsParams effectsParams_{};

    // ==========================================================================
    // Phase 10 C-1 STEP 4 - the spectral-delay SEND (spec C-2, plan 2.4, 3.1)
    // ==========================================================================
    // The component runs 100 % WET as a PARALLEL SEND and is never mixed inline:
    // its internal dry/wet blends a CURRENT-BLOCK dry against a wet that is
    // fftSize samples late (spectral_delay.h:341, :378, :542-544), so an inline
    // setDryWetMix() would put a smeared comb on the whole bus (C-2).
    Krate::DSP::SpectralDelay spectralDelay_{};

    // The fixed-size accumulator (C-2 clause 5). SpectralDelay::process is NOT
    // partition-invariant, so it may only ever see kFxSendChunkSamples samples:
    // the input FIFO collects the post-master-gain bus, a chunk runs whenever a
    // whole one is available, and the return is read out of an output FIFO
    // PRE-FILLED with one chunk of silence. That one-chunk pipeline delay is a
    // fixed 512 samples, absorbed into the send's delay time exactly as C-2
    // clause 3 absorbs the fftSize latency - and likewise NOT reported (FR-005).
    std::vector<float> fxInL_, fxInR_;        // input FIFO,  kFxFifoCapacity
    std::vector<float> fxOutL_, fxOutR_;      // output FIFO, kFxFifoCapacity
    std::vector<float> fxChunkL_, fxChunkR_;  // ONE kFxSendChunkSamples scratch pair
    // fxChunkFill_ is THE chunk grid, and the only one (plan D-3): there is
    // deliberately NO free-running fxPhase_. While bypassed the input FIFO is not
    // written at all (FR-007), so a second counter could not describe the same
    // grid - it would only relocate the claim.
    std::size_t fxInWrite_ = 0, fxInRead_ = 0, fxChunkFill_ = 0;
    std::size_t fxOutWrite_ = 0, fxOutRead_ = 0, fxOutFill_ = 0;

    // FR-008 condition (a): how long the send has been continuously bypassed.
    // Saturating, never wrapping - at 192 kHz a uint64 covers ~3 million years.
    std::uint64_t fxBypassedSamples_ = 0;
    /// Plan D-5. Live post-Aether samples consumed since the send last became
    /// active, so a freeze-forced engage can PRIME before setFreezeEnabled(true).
    std::uint64_t fxLiveSamplesSinceEngage_ = 0;
    std::int64_t fxDrainRemaining_ = 0;    // FR-009a countdown, in samples
    std::int64_t fxSendDrainSamples_ = 0;  // FR-009a window in samples; set in setupProcessing()
    /// FR-009a's ENERGY exit. Peak |sample| of the chunk the send most recently
    /// produced IN THE CURRENT DRAIN. Initialised ABOVE kFxSendDrainFloor so a
    /// drain that has not yet run a chunk can never take that exit - which would
    /// be exactly the tail annihilation FR-008/FR-009a exist to prevent.
    float fxDrainPeak_ = 1.0f;
    /// The return gain the FR-008/FR-009/FR-023a state machine computed for this
    /// process() call. setParamSmootherTargets() targets THIS, not the raw ID
    /// 1410 atomic.
    float fxEffectiveReturnGain_ = 0.0f;
    enum class FxSendState : std::uint8_t { Bypassed, Active, Draining };
    FxSendState fxSendState_ = FxSendState::Bypassed;
    /// FR-008's reset, deferred to the next FILL-CHUNK boundary (plan D-3).
    bool fxResetDue_ = false;
    /// Plan section 3.1's ONE deferred-clear flag. pushEffectsParams() raises it
    /// rather than clearing the FIFOs itself, so the single-clear-site rule is
    /// structural instead of an accident of call order.
    bool fxFifoClearDue_ = false;
    /// FR-030. Built ONCE per process() call, in the pre-slice block, from the
    /// same tempo sample point Phase 9 already uses.
    Krate::DSP::BlockContext fxBlockCtx_{};

    /// FR-007's EXACT prohibition predicate, hoisted once per process() call by
    /// updateEffectsBypassState() (FR-012): `fxSendState_ != Bypassed`, i.e.
    /// "active OR draining". While it is false runSendStage() is not called at
    /// all, which is the only reason the C-6 default configuration costs nothing
    /// (C-3): at the defaults the send has never been active, so it is never
    /// draining either.
    bool fxSendRuns_ = false;

    /// FR-010's EXACT bypass predicate, evaluated ONCE PER process() CALL on the
    /// RAW atomics (FR-012):
    ///     !(width == 100 && wanderDepth == 0 && azimuthDepth == 0)
    /// The exactness is the point: FR-024a makes both depths a PLUGIN-SIDE
    /// multiply precisely so this predicate can read plain scalars on the block
    /// the host wrote them, which a value pushed through BrownianDrift::setDepth
    /// - behind that component's 150 ms kDriftOutputSmoothMs output smoother
    /// (brownian_drift.h:103) - could not be.
    bool fxWanderRuns_ = false;
    /// FR-010a's DISENGAGE arm. fxWanderRuns_ is the raw predicate; THIS is what
    /// runWanderStage()'s early return reads. It keeps the stage running until
    /// every wander smoother has actually reached identity, so writing
    /// kFxWanderDepthId = 0 cannot step the stereo image in one sample.
    bool fxWanderRunsEffective_ = false;
    /// FR-010a's countdown, in samples. Re-armed to fxWanderSettleSamples_ on
    /// EVERY block the raw predicate is true and decremented by the block length
    /// otherwise, so the disengage tail is a STATED sample count rather than a
    /// guess about a smoother nobody can query.
    std::int64_t fxWanderSettleRemaining_ = 0;
    /// Three time constants of the SLOWER of MidSideProcessor::
    /// kDefaultSmoothingMs (10 ms, midside_processor.h:73) and kParamSmoothMs
    /// (20 ms). Sample-rate dependent, so a member and not a constant; set in
    /// setupProcessing().
    std::int64_t fxWanderSettleSamples_ = 0;
    /// ID 1440's raw atomic, hoisted once per process() call beside the
    /// predicates above (FR-012) so the 64-sample control loop reads a plain
    /// float rather than an atomic 32 times per block.
    float fxWidthBase_ = Krate::DSP::MidSideProcessor::kDefaultWidth;

    // --- FR-024's control-grid witness (probe-only; NOT an FR-041 surface) ----
    // FR-041's PUBLIC seam set is closed at seven read surfaces plus the
    // truncation flag, so this is reached ONLY through
    // detail::SeraphisEffectsStageBypassProbe, exactly as T014's component
    // getters are. It exists because plan R-14's defect - a control loop that
    // runs to completion BEFORE one audio call - is invisible in the audio: the
    // drifts are advanced once per process() call and getCurrentValue() is a pure
    // read (brownian_drift.h:212), so every chunk of a single-slice block
    // computes the SAME target and the broken shape sounds identical. The only
    // honest witness is the pairing of a control evaluation with the samples it
    // was applied to, which is what these three record: one entry per sub-chunk,
    // written AFTER that sub-chunk's audio, carrying the two azimuth targets and
    // the sub-chunk's own length. The non-interleaved shape reports ONE entry of
    // length n; a per-sample cos/sin reports n entries of length 1.
    //
    // Cost: two float stores, one uint16 store and one increment per 64 samples
    // of audio - ~0.05 % of the stage's own work.
    static constexpr std::size_t kWanderControlLogCapacity =
        kMaxBlockSamples / kWanderControlChunkSamples;  // 32
    std::array<float, kWanderControlLogCapacity> wanderAzimuthTargetL_{};
    std::array<float, kWanderControlLogCapacity> wanderAzimuthTargetR_{};
    std::array<std::uint16_t, kWanderControlLogCapacity> wanderChunkLengths_{};
    /// Control evaluations THIS process() call. May exceed the log capacity (the
    /// arrays then hold the first 32), which is itself a detectable defect.
    std::size_t wanderControlUpdates_ = 0;

    // ==========================================================================
    // Phase 10 C-1 STEP 5 - stereo wandering (spec C-5)
    // ==========================================================================
    // The two drift SOURCES advance on EVERY block regardless of any bypass
    // state (FR-011, C-3's final clause). A drift that only advanced while the
    // stage was engaged would restart its walk on every re-engage, which reads as
    // the image jumping rather than as one object that kept moving.
    //
    // WHERE they advance depends on whether the stage runs, and that is SC-017's
    // block-size invariance rather than an optimisation: while the stage runs
    // they step by exactly one 64-sample control chunk per ABSOLUTE grid
    // boundary, from inside runWanderStage(), so the value at boundary k is a
    // pure function of k; while it is skipped the pre-slice block advances the
    // whole call at once, because nothing else would advance them at all. The
    // two paths are mutually exclusive.
    Krate::DSP::BrownianDrift widthDrift_{};
    Krate::DSP::BrownianDrift azimuthDrift_{};

    // The GLOBAL width stage (spec Non-goals: SeraphisVoice already owns a
    // PER-VOICE MidSideProcessor; this is a different instance with a different
    // job). Driven on the 64-sample control grid from inside runWanderStage(),
    // which is also the only place its per-sample smoother is advanced
    // (midside_processor.h:186-192 - it advances ONLY inside process()).
    Krate::DSP::MidSideProcessor globalMs_{};

    // C-5's azimuth pan pair. DELIBERATELY NOT IN classBSmoothers(): they carry
    // no ParamID, they are plugin-local ramps, and runWanderStage()'s per-sample
    // .process() is their SOLE advance - which is exactly why putting them in
    // that array would double their rate and halve kParamSmoothMs (the invariant
    // written out beside the three class-(b) members below).
    Krate::DSP::OnePoleSmoother azimuthGainLSm_{1.0f};
    Krate::DSP::OnePoleSmoother azimuthGainRSm_{1.0f};

    /// FR-010a's ENGAGE arm. The stage's own "was I running last call?" latch,
    /// assigned fxWanderRunsEffective_ at the end of EVERY runWanderStage() call.
    bool fxWanderWasActive_ = false;

    // --- FR-041b: the ONLY readable source of the four spectral states --------
    // The five factory states are built ONCE IN THE CONSTRUCTOR - not in
    // setupProcessing() - and are IMMUTABLE thereafter, so both threads may read
    // them without synchronisation. makeFactoryState() is documented
    // "CONFIGURATION-TIME, not audio-thread: ... ~200 std::pow/std::exp calls"
    // (spectral_state.h:371-372), so it may not be called from
    // processParameterChanges(); and it needs no sample rate, so deferring the
    // table to prepare() buys nothing and costs the before-prepare window in
    // which getState() would write four VALID, EMPTY payloads.
    std::array<Krate::DSP::SpectralState, 5> factoryStates_{};

    std::array<Krate::DSP::SpectralState, 4> spectralSlots_{};  // audio-thread-owned
    // THREE staging buffers, not one: two would let a second setState() write the
    // buffer the audio thread is copying (plan 3.7's writer interlock).
    std::array<std::array<Krate::DSP::SpectralState, 4>, 3> spectralSlotsStaging_{};
    std::atomic<int> spectralSlotsHandoff_{-1};    // published buffer index, or -1
    std::atomic<int> spectralSlotsConsuming_{-1};  // buffer being copied, or -1
    int stagingWriteCursor_ = 0;                   // message-thread-only
    bool spectralStatesPending_ = false;           // FR-046
    // FR-046: which voices have NOT yet accepted. Bit v selects voices_[v]. The
    // retry is PER-VOICE: a whole-pool retry re-runs buildSanitized (a 64-entry
    // std::log2 pass) on every accepting voice too, ~3840 std::log2 per block for
    // the whole of a held note plus its release.
    std::uint16_t spectralRetryMask_ = 0u;

    // --- FR-042: two INDEPENDENT on-change generation-counter pairs -----------
    // Separate so an AE change does not force a 37-setter x 16-voice fan-out, and
    // a VP change does not re-push the ten reverb controls.
    std::size_t voiceParamGeneration_ = 0;
    std::size_t lastAppliedVoiceParamGeneration_ = kGenerationSentinel;
    std::size_t aetherParamGeneration_ = 0;
    std::size_t lastAppliedAetherParamGeneration_ = kGenerationSentinel;

    // --- FR-043 / FR-045 on-change trackers ----------------------------------
    std::array<float, Krate::DSP::SeraphisMacroMatrix::kNumTargets> lastPushedBase_{};
    bool lastPushedBaseValid_ = false;  // ONE flag, not 27 - pushAllSurfaces clears it
    Krate::DSP::SeraphisMacroValues lastPushedMacros_{};
    bool lastPushedMacrosValid_ = false;
    int lastPushedSeedIndex_ = -1;                              // never a legal index
    std::array<int, 4> lastPushedSlotStateId_{-1, -1, -1, -1};  // CFG change guard
    // The same guard for kMorphStateCountId (408). Without it a host automating
    // that lane re-raises spectralStatesPending_ every block, and each raise is a
    // 16-voice x 4-slot fan-out - 4096 std::log2 - for an unchanged value.
    int lastPushedStateCount_ = -1;  // never a legal count ([2, 4])
    bool lastPushedFreeze_ = false;
    bool lastPushedFreezeValid_ = false;
    // Phase 10 D-2. The on-change tracker for the ONE composed output-saturation
    // value, and it REPLACES Phase 9's lastPushedSoftLimitValid_: the quantity
    // that is now change-detected is `softLimit ? kFxSaturationId : 0`, not the
    // soft-limit flag on its own. Detecting on the flag as well would re-push
    // 0.15f the moment ID 2 was toggled off and on, silently reverting whatever
    // amount ID 1400 had installed (spec FR-021 / plan D-2).
    float lastPushedSaturation_ = 0.0f;
    bool lastPushedSaturationValid_ = false;
    // Phase 10 FR-026 / FR-027. The send's OWN seed tracker, separate from
    // lastPushedSeedIndex_ (which guards engine_/reverb_): setupProcessing()
    // seeds and resets spectralDelay_ itself, so the first process() must not
    // re-run that burst for an unchanged seed. -1 is never a legal index.
    int lastPushedFxSeedIndex_ = -1;

    // Phase 10 FR-022 / FR-025. ONE tracker per setter pushEffectsParams() owns,
    // plus ONE shared first-call flag.
    //
    // WHY A VALIDITY FLAG AND NOT PER-FIELD SENTINELS. setupProcessing() pushes
    // the send's FFT size, dry/wet, spread curve and seed, and NOTHING ELSE - so
    // after every prepare the component still holds its OWN construction defaults
    // (spectral_delay.h:880-893: feedback 0, diffusion 0, stereoWidth 0,
    // noteValueIndex 4), which are not the C-6 registered defaults. The flag makes
    // the first process() after every prepare deliver the whole registered set
    // exactly once, and every block after that is on-change only. It is cleared
    // again by pushAllSurfaces() for the same reason lastPushedSaturationValid_ is:
    // a preset load must push through, and setState() writes the atomics before
    // the release store the audio thread consumes.
    bool lastPushedFxValid_ = false;
    float lastPushedFxDelayTimeMs_ = 0.0f;
    float lastPushedFxSpreadMs_ = 0.0f;
    float lastPushedFxDiffusion_ = 0.0f;
    float lastPushedFxStereoWidth_ = 0.0f;
    /// The RAW registered feedback, NOT the compensated value that was pushed -
    /// so the change test is against what the host wrote, and FR-016a's divide is
    /// recomputed from the pair rather than inverted.
    float lastPushedFxFeedback_ = 0.0f;
    float lastPushedFxTilt_ = 0.0f;
    bool lastPushedFxSync_ = false;
    int lastPushedFxSyncNote_ = 0;
    int lastPushedFxSpreadDirection_ = 0;
    float lastPushedFxWanderRate_ = 0.0f;
    /// ID 1430's tracker holds the COMPOSED `freezeReady` predicate of plan D-5,
    /// NOT the raw atomic: what is change-detected must be what was PUSHED, or the
    /// priming window would be re-decided every block and the deferred engage would
    /// push on the toggle block after all. false matches the component's own
    /// construction default (spectral_delay.h freezeEnabled_), so the first push
    /// after a prepare is a no-op at the C-6 default.
    bool lastPushedFxFreezeReady_ = false;

    // --- FR-047: the one force-push request, raised off the audio thread ------
    std::atomic<bool> forcePushAllPending_{false};

    // FR-059 / plan 3.3. Latches the settling->settled transition so the EXACT
    // target value is pushed once. Without it the chunk on which the smoother
    // snaps current_ = target_ is also the chunk isComplete() turns true, the
    // settling clause goes false, the generation compare is equal, and the voice
    // is left permanently ~1e-4 short of target.
    bool wasVoiceClassBSettling_ = false;

    // FR-047 / plan 3.4. A PRESET LOAD SNAPS; it does not ramp. Raised by
    // pushAllSurfaces() and consumed by advanceParamSmoothers(), which snaps all
    // nine class-(b) smoothers to their freshly set targets instead of ramping.
    // SC-023 clause 4 asserts every route's read-back after ONE block, and a
    // 20 ms class-(b) ramp over a 512-sample block reaches only ~93 % of target -
    // the exact-equality read-back would fail for a correct implementation.
    bool snapParamSmoothers_ = false;

    // --- FR-059(b): PHASE 9's nine class-(b) smoothers (plan 3.5.3) -----------
    // Phase 9's class (b) is EXACTLY nine IDs: 100, 101, 102, 103, 104, 801,
    // 802, 1215, 1216 - Phase 10's FR-038b clause 2 adds three more, declared
    // immediately below. Each is seeded with its own registered default so the
    // very first render starts from the shipped value rather than ramping from 0 (the
    // prepare-time snapParamSmoothers_ raised by pushAllSurfaces() makes that
    // belt-and-braces, but a wrong seed would be visible in the window between
    // construction and the first process()).
    Krate::DSP::OnePoleSmoother resonanceSm_{0.7f};     // ID 801, VP-routed
    Krate::DSP::OnePoleSmoother bodyDampingSm_{0.25f};  // ID 802, MB-routed
    Krate::DSP::OnePoleSmoother breathDepthSm_{0.20f};  // ID 1215, MB-routed
    Krate::DSP::OnePoleSmoother tideDepthSm_{0.20f};    // ID 1216, MB-routed
    // IDs 100-104, in SeraphisMacro order: Dream, Bloom, Dissolve, Gravity,
    // Entropy. Seeded at Phase 7's FR-060 neutral (seraphis_macro_matrix.h:
    // 122-128), which is also Phase 8's registered default set.
    std::array<Krate::DSP::OnePoleSmoother, 5> macroSm_{
        {Krate::DSP::OnePoleSmoother{0.0f}, Krate::DSP::OnePoleSmoother{0.0f},
         Krate::DSP::OnePoleSmoother{0.0f}, Krate::DSP::OnePoleSmoother{0.5f},
         Krate::DSP::OnePoleSmoother{0.0f}}};

    // --- Phase 10 FR-038b clause 2: THREE MORE class-(b) smoothers ------------
    // IDs 1410 (send return gain), 1441 (wander depth) and 1443 (azimuth depth).
    // All three join classBSmoothers(), which is what widens its return type
    // from 9 to 12 and puts these ramps into anyClassBSmootherUnsettled().
    //
    // INVARIANT, WRITTEN HERE BECAUSE THIS IS WHERE IT IS BROKEN:
    //
    //     NO SMOOTHER MAY BE IN classBSmoothers() AND ALSO BE .process()-ed.
    //
    // advanceParamSmoothers() advances EVERY element of classBSmoothers() by the
    // sub-slice's own sample count, and it runs immediately BEFORE renderSlice()
    // in the slice loop. A member that is additionally .process()-ed per sample
    // inside a stage therefore advances 2n samples per n rendered while that
    // stage runs and n while it does not: kFxReturnRampMs would be halved AND the
    // ramp rate would become state-dependent. Every consumer must therefore READ
    // these with getCurrentValue() (smoother.h:191) and never call process().
    //
    // The shipped precedent is explicit and points the same way: masterGain_ -
    // the ONE smoother advanced per output sample in renderSlice() - is
    // deliberately NOT a member of classBSmoothers().
    Krate::DSP::OnePoleSmoother fxReturnGainSm_{0.0f};   // ID 1410, the FR-008/FR-009 ramp
    Krate::DSP::OnePoleSmoother fxWanderDepthSm_{0.0f};  // ID 1441
    Krate::DSP::OnePoleSmoother fxAzimuthDepthSm_{0.0f}; // ID 1443

    // FR-059a. Set ONLY by detail::SeraphisParamSmootherBypassProbe, which the
    // library never defines. false on every shipping path.
    bool paramSmootherBypass_ = false;

    // Plan 3.5.2. An ABSOLUTE sample counter, continuous across slices AND
    // across process() calls, so the 64-sample delivery grid is block-size
    // independent BY CONSTRUCTION - the property processor.cpp's master-gain
    // banner demands and a per-block advance cannot have. Reset only at
    // setupProcessing(). uint64: at 192 kHz it wraps after ~3 million years.
    std::uint64_t controlPhase_ = 0;

    // --- FR-056 synced travel -------------------------------------------------
    float lastSyncedTravelRate_ = -1.0f;  // sentinel: below kMinTravelRate

    Krate::DSP::OnePoleSmoother masterGain_{1.0f};

    // NO [[maybe_unused]] on any member below. Every one is read on a live path,
    // so the attribute would only serve to hide a genuinely dead private field
    // from Clang's -Wunused-private-field on the macOS/Linux CI legs.
    bool anySamplesSincePrepare_ = false;  // FR-024a cl.3 snap seam
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    std::size_t lastPushedPolyphony_ = 0;  // FR-024a cl.1 on-change tracker
    // The soft-limit GATE as prepare() saw it. Phase 10 D-2 narrowed its role:
    // it is no longer an on-change tracker (lastPushedSaturation_ is), it is the
    // value setupProcessing()'s composed prepare-time push is gated on.
    bool lastPushedSoftLimit_ = true;
    std::size_t setPolyphonyCalls_ = 0;    // test seam only (read by the accessor above)

    // --- FR-041a test-only counters (plain size_t; audio-thread-written) ------
    std::size_t applyVoiceParamsCalls_ = 0;
    std::size_t applySpectralStatesCalls_ = 0;
    std::size_t applySpectralStatesAttempts_ = 0;  // ATTEMPTS, incl. failed retries
    std::size_t applyAetherParamsCalls_ = 0;
    std::size_t setTargetBasePushes_ = 0;
    std::size_t spectralHandoffConsumes_ = 0;  // SC-023 clause 5
    std::size_t engSeedPushes_ = 0;            // engine_->setSeed + reverb_->setSeed
    std::size_t engSoftLimitPushes_ = 0;       // engine_->setOutputSaturation
    std::size_t engFreezePushes_ = 0;          // engine_->setAtmosphereFreeze

    // --- Phase 10 FR-040: the three probe flags -------------------------------
    // Set ONLY by detail::SeraphisEffectsStageBypassProbe, which the library
    // never defines. false on every shipping path.
    //
    // STILL NO [[maybe_unused]], and the banner above still holds. All three now
    // have readers on the render path: capabilities 1 and 2 in renderSlice(), and
    // capability 3 in runSendStage()'s mix step, where it snaps the FR-008/FR-009
    // return-gain ramp instead of reading its ramped value.
    bool effectsStageBypassed_ = false;     // capability 1: skip C-1 steps 4 + 5
    bool effectsStageAfterOutput_ = false;  // capability 2: run step 5 AFTER step 6
    bool effectsReturnRampSnap_ = false;    // capability 3: snap the return-gain ramp

    // --- Phase 10 FR-041 test-only counters (plain size_t/double/bool) --------
    // Constitution II: the master switch for clause 1's clock reads and clause
    // 6's tap copy. FALSE on every shipping path - see
    // setEffectsStageInstrumentedForTest() for why it is not a compile-time
    // switch (FR-039: processor.cpp is compiled ONCE into seraphis_tests).
    bool effectsStageInstrumented_ = false;
    double effectsStageNs_ = 0.0;              // clause 1, accumulated per slice
    std::size_t effectsStageProcessCalls_ = 0; // clause 1's divisor: per process() CALL
    std::size_t spectralDelayResets_ = 0;      // clause 2
    std::size_t effectsPushes_ = 0;            // clause 3
    std::size_t widthDriftBlocks_ = 0;         // clause 4
    std::size_t bypassPredicateEvals_ = 0;     // clause 5
    std::size_t sendChunks_ = 0;               // clause 7

    // FR-041 clause 6 / FR-028: the pre-output-stage tap, sized ONCE at
    // setupProcessing() to the same CONSTANT 2048 as the scratch below - never to
    // the host block, which may legally be larger (the truncation flag is what
    // makes that visible instead of silent).
    std::vector<float> preOutTapL_, preOutTapR_;
    std::size_t preOutTapCursor_ = 0;   // write position within THIS process() call
    std::size_t preOutTapSize_ = 0;     // samples actually taped by THIS call
    bool preOutTapTruncated_ = false;   // this call delivered > kMaxBlockSamples

    // FR-028: sized ONCE at setupProcessing(), to the CONSTANT 2048.
    std::vector<float> dryL_, dryR_, wetL_, wetR_;
    // Filled by renderSlice()'s bloom lifecycle (FR-024 step 6).
    std::array<float, Krate::DSP::SeraphisEngine::kBloomPartialCap> bloomPartials_{};
};

// FR-067: unique_ptr ownership keeps the object small enough for a stack local
// in tests. If this ever fails, the tests must heap-allocate the processor
// (seraphis_engine.h:119-122).
static_assert(sizeof(Processor) < 64u * 1024u,
              "FR-067: Processor must stay small; the 771 968 B engine lives on the heap");

}  // namespace Seraphis
