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

// Phase 11 C-2 (T006/T008). The DataExchange payload published once per
// process() call. It is a POD over <cstdint> ONLY and names no processor type,
// so the controller includes it too - the sanctioned shared-POD exception
// (plugins/membrum/src/processor/meters_block.h is the precedent), not a
// cross-boundary include.
#include "processor/cloud_frame.h"

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

// Phase 11 FR-011 (plan 5.2). Forward-declared, exactly as Membrum does
// (plugins/membrum/src/processor/processor.h:33-34), so this header stays cheap
// to include; public.sdk/source/vst/utility/dataexchange.h is pulled in by
// processor.cpp alone. The unique_ptr member below is legal over an incomplete
// type because ~Processor() is defined out of line (processor.cpp:404).
namespace Steinberg::Vst {
class DataExchangeHandler;
}  // namespace Steinberg::Vst

// Phase 11 C-5 (plan 6.1/6.2, T010). The controller -> processor wire format.
// FORWARD-DECLARED here and INCLUDED by processor.cpp alone: applyEditMessage
// takes it by const&, which needs only an incomplete type, and that keeps
// src/ui/ out of this header's include set even though edit_message.h is a POD
// over <cstdint> that the processor is expressly allowed to see.
namespace Seraphis::UI {
struct EditMessage;
}  // namespace Seraphis::UI

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

/// Phase 11 C-2 clause 4(b). The level below which a voice that the allocator
/// already reports Idle no longer holds the cloud view's focus.
///
/// SeraphisVoice::getCurrentLevel() (seraphis_voice.h:815) is a linear
/// amplitude, so this is -80 dBFS: low enough that the whole release tail still
/// animates, high enough that a decayed slot cannot pin the focus forever once
/// the envelope has run out. It is a DISPLAY threshold only - nothing audible
/// keys on it.
inline constexpr float kCloudFrameSilenceLevel = 1.0e-4f;

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

/// Phase 11 T008. DECLARED HERE, DEFINED ONLY BY THE CLOUD-FRAME TU
/// (tests/integration/cloud_frame_test.cpp). Same shape and same rationale as
/// the two probes above.
///
/// SOLE capability: write Processor::partialMaskBits_ /
/// Processor::partialPanOverrideBits_, the two override bitmasks
/// publishCloudFrame() mirrors into CloudFrame::maskBits /
/// CloudFrame::overriddenBits.
///
/// WHY IT EXISTS. SC-006 arm (e) asserts that mirroring, but the SHIPPING
/// writers of those two masks are the C-5 edit channel's kinds 2 and 3
/// (Processor::notify -> applyEditMessage), which land in T010 - two groups
/// later. Without a probe, arm (e) could only be written against the default
/// all-zero state, which asserts nothing about the mirroring. A probe keeps the
/// criterion observable at T008 WITHOUT growing the shipping seam set: the
/// library never defines this struct, so no shipping build can name it.
///
/// Once T010/T011 land, this probe stays as the direct-write path arm (e) uses;
/// SC-033 exercises the real message path end to end.
///
/// ODR swept this session:
/// `grep -rn "SeraphisCloudFrameProbe" dsp/ plugins/ tools/` -> 0 hits.
struct SeraphisCloudFrameProbe;
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

    // --- Phase 11 FR-011 (plan 5.2). The DataExchange queue's lifecycle. ------
    // Shape copied verbatim from Membrum
    // (plugins/membrum/src/processor/processor.cpp:1136-1164, :1110-1126): the
    // handler is BUILT in connect() and RELEASED in disconnect(), and the QUEUE
    // is opened/closed by setActive(), which is already overridden above.
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;

    // --- Phase 11 C-5 / FR-036 (plan 6.2, T010). THE EDIT CHANNEL. -----------
    // The processor had no notify() before Phase 11. This one handles exactly
    // ONE message ID ("SeraphisEdit") carrying exactly ONE binary attribute (a
    // POD UI::EditMessage); everything else delegates to the SDK base.
    //
    // MESSAGE THREAD ONLY - the same thread setState() already writes the
    // three-buffer staging ring from, which is why the Phase 9 writer interlock
    // holds unchanged with no second interlock and no lock. It is never reached
    // from process().
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

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

    // =========================================================================
    // Phase 11 C-2 clause 7 + the frame observable (plan 5.3). Same discipline
    // as the two blocks above: plain scalars written only from the audio thread
    // and read only from the test thread between process() calls.
    // =========================================================================

    /// SC-007, SC-010. Incremented ONCE PER process() CALL that reached
    /// publishCloudFrame() with the clause-6 gate OPEN - independently of
    /// whether a DataExchange queue exists at all. The gate is the ONLY
    /// short-circuit ahead of it (plan 5.3, "the handler is NOT a
    /// precondition"): ProcessorFixture never calls connect(), so a
    /// handler-conditioned counter would read 0 in every plugin-side test on a
    /// CORRECT implementation.
    [[nodiscard]] std::size_t cloudFramePublishAttemptCountForTest() const noexcept {
        return cloudFramePublishAttempts_;
    }
    /// RECORDED, NEVER GATING (C-2 clause 7). Incremented when an attempt found
    /// no transport: no handler at all, or getCurrentOrNewBlock() handed back an
    /// invalid/short block because the host queue is full. At 512/48 kHz the
    /// ~94 Hz publish rate deliberately outruns the 30 Hz consume rate, so a
    /// rising count is expected steady-state behaviour, not a fault.
    [[nodiscard]] std::size_t cloudFrameSkippedBlockCountForTest() const noexcept {
        return cloudFrameSkippedBlocks_;
    }
    /// SC-007's STRICT `>`. renderSlice() invocations - the slice loop
    /// subdivides on every MIDI event, on the 2048 cap and, while any class-(b)
    /// smoother is unsettled, on the absolute 64-sample grid, so this must
    /// exceed the publish-attempt count on any render that carries either.
    [[nodiscard]] std::size_t renderSliceCountForTest() const noexcept { return renderSlices_; }
    /// SC-001's negative control. Writes THE SAME atomic the C-5 kind-0
    /// editor-gate message writes (T010), so both arms of that criterion run in
    /// one build, one process and one Processor instance with only the gate
    /// differing - Phase 10's SC-002 shape.
    void setCloudFrameGateForTest(bool open) noexcept {
        cloudFrameEnabled_.store(open, std::memory_order_relaxed);
    }
    /// SC-006 arm (i), FR-011. TRUE only between connect() and disconnect().
    [[nodiscard]] bool dataExchangeHandlerLiveForTest() const noexcept {
        return dataExchangeHandler_ != nullptr;
    }

    /// SC-006 / SC-008 / SC-014 / SC-017 - THE FRAME OBSERVABLE.
    ///
    /// pendingFrame_ is filled on EVERY attempt, BEFORE getCurrentOrNewBlock()
    /// is consulted (publishCloudFrame()'s normative body order), so it is the
    /// frame the producer WOULD have published - including on the skipped
    /// attempts. That is what makes the four content criteria runnable in a
    /// headless harness that never opens a queue.
    ///
    /// It is a const& to a member the AUDIO thread writes: every test must read
    /// it BETWEEN process() calls, never concurrently. The headless suites are
    /// single-threaded and already work that way.
    [[nodiscard]] const CloudFrame& lastPublishedFrameForTest() const noexcept {
        return pendingFrame_;
    }
    [[nodiscard]] std::uint32_t cloudFrameSequenceForTest() const noexcept {
        return cloudFrameSequence_;
    }

    /// SC-011's lock-free arm. ANDs is_lock_free() over every Phase 11 atomic
    /// that crosses the message/audio boundary: the C-2 clause 6 gate, the two
    /// FR-030 override bitmasks, the release/acquire handshake flag and the
    /// staged pan array (element 0 stands for all 64 - they are one array of one
    /// type, so a locking implementation would apply to every element).
    ///
    /// The constitution's rule is that only std::atomic_flag is GUARANTEED
    /// lock-free, so "lock-free on x86-64/arm64" is asserted at runtime by
    /// tests/integration/ui_perf_test.cpp rather than assumed.
    [[nodiscard]] bool phase11AtomicsAreLockFreeForTest() const noexcept {
        return cloudFrameEnabled_.is_lock_free() && partialMaskBits_.is_lock_free()
               && partialPanOverrideBits_.is_lock_free()
               && partialOverridesPending_.is_lock_free()
               && partialPanStaging_[0].is_lock_free();
    }

    /// SC-009(b) / SC-010(b) stage instrumentation, modelled exactly on Phase
    /// 10's effectsStageNsForTest() / effectsStageProcessCallsForTest() pair.
    ///
    /// The scoped timer opens OUTSIDE the cloudFrameEnabled_ predicate and this
    /// divisor counts EVERY process() call, not every publish - which is what
    /// keeps SC-010(b) honest: with the gate closed the measured stage time is
    /// the cost of TESTING the gate.
    [[nodiscard]] double cloudFrameStageNsForTest() const noexcept { return cloudFrameStageNs_; }
    [[nodiscard]] std::size_t cloudFrameStageProcessCallsForTest() const noexcept {
        return cloudFrameStageProcessCalls_;
    }
    /// Per-INSTANCE, default OFF - so no shipping path ever reads the clock, and
    /// the [.perf] arms that are not measuring this stage pay nothing.
    void setCloudFrameInstrumentedForTest(bool on) noexcept { cloudFrameInstrumented_ = on; }

    // =========================================================================
    // Phase 11.5 Step 0 - whole-process() decomposition instrumentation.
    //
    // WHY IT EXISTS. Phase 11's OE-1 decomposed the measured 31.74 % as "chain
    // 22.04 % + effects 0.45 % + ~9.2 points of remainder", but the chain-only
    // subject (param_perf_test.cpp:1833-1878) runs a DIFFERENT configuration
    // (body material, diffusion FFT size) from the plugin - the remainder is
    // arithmetic over non-identical scenarios, not a measured cost. These timers
    // measure the split INSIDE the failing subject itself, so Phase 11.5
    // optimizes what is measured rather than what was inferred.
    //
    // Same discipline as effectsStageInstrumented_ / cloudFrameInstrumented_
    // (Constitution II): the gate is FALSE on every shipping path, so the
    // shipped plugin executes no clock read here - it pays one always-false
    // branch per timed region. Diagnostic only: no criterion gates on these.
    // =========================================================================
    enum class DecompStage : std::size_t {
        Params,      ///< processParameterChanges(), top of process()
        PreSlice,    ///< force-push consume .. masterGain target (all once-per-call pushes)
        SlicePush,   ///< advanceParamSmoothers + pushVoiceParams + pushMacroSurfaces, per slice
        MacroApply,  ///< macros_.apply + spread tracker + computeAetherTargets/apply, per slice
        Engine,      ///< engine_->processStereoBlock (the voice sum)
        Reverb,      ///< reverb_->processStereoBlock (Aether)
        MasterGain,  ///< the per-sample master-gain loop
        Output,      ///< engine_->processOutputStage (saturator + limiter)
        Count
    };
    void setProcessDecompInstrumentedForTest(bool on) noexcept {
        processDecompInstrumented_ = on;
    }
    [[nodiscard]] double decompNsForTest(DecompStage stage) const noexcept {
        return decompNs_[static_cast<std::size_t>(stage)];
    }
    [[nodiscard]] std::size_t decompProcessCallsForTest() const noexcept {
        return decompProcessCalls_;
    }
    void resetDecompForTest() noexcept {
        decompNs_.fill(0.0);
        decompProcessCalls_ = 0;
    }

    // =========================================================================
    // Phase 11 C-10 / FR-037..FR-039 (plan section 4, T013) - the COMPOSED
    // effects targets. SC-021(b)/(c)'s observables.
    //
    // WHY THESE THREE AND NOT effectsPushCountForTest() (spec D-2). The push
    // counter is incremented only inside pushEffectsParams(), whose ID set does
    // NOT contain 1410 or 1441 - so it provably cannot move for a macro-driven
    // change to either, and a criterion written against it would fail on a
    // CORRECT implementation. These read the composition itself instead.
    //
    // Same discipline as the two blocks above: plain scalars written only from
    // the audio thread, read only from the test thread between process() calls.
    // =========================================================================

    /// SC-021(b)/(c). The composed send level the pre-slice block computed for
    /// the most recent process() call - i.e. what updateEffectsBypassState()
    /// actually read, RAW (the consuming clamp is applied at the read site).
    [[nodiscard]] float composedFxDelaySendForTest() const noexcept;
    /// SC-021(b)/(c). Likewise for the wander depth read by
    /// setParamSmootherTargets() and by FR-010's ENGAGE predicate.
    [[nodiscard]] float composedFxWanderDepthForTest() const noexcept;
    /// FR-012's cadence witness: incremented EXACTLY ONCE PER process() CALL
    /// that reached the pre-slice block, never once per renderSlice().
    [[nodiscard]] std::size_t composedEffectsRecomputeCountForTest() const noexcept;

    // =========================================================================
    // Phase 11 C-5 - the edit channel's observables (plan 6.2, T010).
    //
    // All four are MESSAGE-THREAD state, read by tests between process() calls
    // on a single-threaded harness. None is reachable from process().
    // =========================================================================

    /// SC-018. The message-thread mirror of the four morph slots - the array
    /// stageSlotEdit() seeds the staging buffer from and writes back into.
    /// Out-of-range slots return slot 0 rather than reading past the end, the
    /// same shape spectralSlotForTest() uses.
    [[nodiscard]] const Krate::DSP::SpectralState& spectralAuthoringSlotForTest(
        int slot) const noexcept {
        const int s = (slot < 0 || slot >= 4) ? 0 : slot;
        return spectralSlotsAuthoring_[static_cast<std::size_t>(s)];
    }
    /// SC-018's "the ring index is -1 or in [0,3)" arm. A fuzzed notify() must
    /// never leave a published index outside the ring.
    [[nodiscard]] int spectralSlotsHandoffForTest() const noexcept {
        return spectralSlotsHandoff_.load(std::memory_order_acquire);
    }
    /// SC-025 arm 3. ACCEPTED stageSlotEdit publishes. A dropped kind-4 (no live
    /// kind-7 snapshot) and a rejected mutation both leave this unmoved, which is
    /// what distinguishes "dropped" from "applied and happened to be a no-op".
    [[nodiscard]] std::size_t editStageWriteCountForTest() const noexcept {
        return editStageWrites_;
    }
    /// C-5 kind 6. Session state, never a ParamID and never serialized.
    [[nodiscard]] int selectedEditSlotForTest() const noexcept { return selectedEditSlot_; }
    /// Q2. TRUE only between a kind-7 BlendBegin and the kind-6 / kind-7 that
    /// ends the gesture.
    [[nodiscard]] bool blendSnapshotValidForTest() const noexcept { return blendSnapshotValid_; }

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
    // Phase 11 T008. The seam SC-006 arm (e) needs before the C-5 edit channel
    // (T010) supplies the shipping writer of the two override bitmasks.
    // Declared above this class and defined ONLY by
    // tests/integration/cloud_frame_test.cpp.
    friend struct detail::SeraphisCloudFrameProbe;

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
    void pushMacroSurfaces() noexcept;           // FR-043 (macros + the 27 VP/AE bases)
    /// Phase 11 C-10 / FR-038 / FR-039. The two Effects-owned MB bases, split out
    /// of pushMacroSurfaces()' loop because they must be current BEFORE
    /// `composedEffects_` is computed - the loop runs per SLICE, the composition
    /// runs once per process() call ABOVE it, and for these two targets the base
    /// IS the raw deep atomic. Called from the pre-slice block only. See the
    /// definition's banner for the three Phase 10 properties a lagged deep path
    /// breaks. Shares lastPushedBase_[] / lastPushedBaseValid_ with that loop, so
    /// there is still exactly ONE base writer per target and SC-007's counter
    /// table is unmoved.
    void pushEffectsMacroBases() noexcept;
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

    /// Phase 11 FR-030 / FR-043 (plan 6.3, T011). THE ONE CONSUMER of the
    /// per-partial override table - the AUDIO-THREAD half of the C-5 kinds 2/3
    /// deferral.
    ///
    /// IT WALKS ALL 64 INDICES AND PUSHES BOTH MASK POLARITIES. Walking only the
    /// SET mask bits is a defect, not an optimisation: kinds 2 and 3 make no
    /// engine call, so this is the ONLY audio-thread path to
    /// SeraphisEngine::setPartialMaskAllVoices, and re-issuing `active = false`
    /// for each set bit means CLEARING a bit produces no engine call at all and
    /// HarmonicCloud::masked_[i] stays true forever (SC-033's unmask half).
    /// clearPartialMaskAllVoices() is deliberately NOT on this path - it is the
    /// FR-033 fan-out surface and would wipe a mask this table still holds.
    ///
    /// MASK POLARITY: HarmonicCloud::setPartialMask's body is
    /// `masked_[index] = !active` (harmonic_cloud.h:1082-1089), so
    /// `active == true` is AUDIBLE. partialMaskBits_ uses the OPPOSITE, plugin-side
    /// convention (bit set <=> masked, C-2), hence the `!masked` at the call.
    ///
    /// @par Thread ownership: AUDIO THREAD, or the host thread with the audio
    ///      thread stopped (the two `setActive`/`setupProcessing` sites, legal for
    ///      the reason setActive's own banner states). NEVER the message thread -
    ///      the fan-outs write HarmonicCloud state process() reads and writes.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free. Two
    ///      relaxed atomic loads plus a bounded 64-iteration fan-out; the mask
    ///      term is 64 x 16 plain byte stores and the pan term is
    ///      popcount(panBits) x 16 x 2 trig calls (updatePanGains ->
    ///      equalPowerGains, crossfade_utils.h:50-53). SC-014 arm 7 measures the
    ///      64-override worst case in T023.
    void repushPartialOverrides() noexcept;

    /// Phase 11 C-2 / FR-012 / FR-015 (plan 5.3). THE CLOUD-FRAME PRODUCER.
    ///
    /// Called EXACTLY ONCE per process() call, AFTER the slice loop, and NEVER
    /// from renderSlice(): the slice loop subdivides on every MIDI event, on the
    /// 2048 cap and - while any class-(b) smoother is unsettled - on the
    /// absolute 64-sample grid, so a per-slice publish would issue up to 8x the
    /// frames for one block and exhaust the 4-block queue inside one call. This
    /// is the same divisor correction Phase 10 made for its stage counter
    /// (effectsStageProcessCalls_).
    ///
    /// BODY ORDER IS NORMATIVE (plan 5.3): gate -> attempt counter -> focus
    /// voice -> fill pendingFrame_ -> transport. Only the GATE short-circuits; a
    /// null handler does NOT, it is accounted as a skipped block. See
    /// lastPublishedFrameForTest() for why.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free,
    ///      I/O-free. A bounded <= 64-iteration read loop over const accessors
    ///      that are array indexes with a bounds test, plus one 808-byte memcpy.
    ///      NO TRANSCENDENTAL: fundamentalHz is a plain member read
    ///      (HarmonicCloud::getFundamentalHz(), harmonic_cloud.h:405), not a
    ///      440 * exp2((note - 69)/12) computation.
    void publishCloudFrame() noexcept;
    void updateSyncedTravelRate(const Steinberg::Vst::ProcessContext* ctx) noexcept;  // FR-056
    void pushAllSurfaces(SurfaceInvalidation scope) noexcept;                         // FR-047
    /// FR-047. The ONLY thing setState() writes toward the audio thread: a
    /// single release store. pushAllSurfaces()' body runs from process().
    void requestPushAllSurfaces() noexcept;
    /// Plan 3.7 step 1. The first of {cursor, cursor+1, cursor+2} mod 3 that is
    /// neither published nor being consumed. Message thread only.
    [[nodiscard]] std::size_t pickStagingBuffer() noexcept;

    // --- Phase 11 C-5 (plan 6.2, T010). MESSAGE THREAD ONLY. -----------------

    /// C-5 clause 5 + the per-kind dispatch. Validation FIRST (unknown kind,
    /// out-of-range slot/index, non-finite a/b - each a silent return), then the
    /// eight-row table.
    ///
    /// KINDS 2 AND 3 MAKE NO ENGINE CALL. Spec C-5 clause 1 (spec.md:656-658)
    /// says they "call the C-4 fan-outs directly"; that sentence is OVERRULED
    /// (plan 6.2, scheduled for correction in T026 as D-9 row 9b). The fan-outs
    /// write HarmonicCloud state process() concurrently reads and writes, so
    /// calling them from here is a data race - "allocates nothing, blocks
    /// nothing" answers allocation, not concurrency. They stage into
    /// partialPanStaging_ / partialMaskBits_ / partialPanOverrideBits_ and
    /// publish partialOverridesPending_ instead; T011 owns the consuming side.
    ///
    /// @par Real-Time Safety: never reached from process(). Allocation-free,
    ///      lock-free, exception-free regardless; the heaviest operation is one
    ///      4 x sizeof(SpectralState) = 2160-byte POD copy plus one mutator.
    void applyEditMessage(const UI::EditMessage& message) noexcept;

    /// Plan 6.2 steps 1-6. THE ONE function that touches the staging ring on
    /// behalf of the edit channel, reusing setState()'s published sequence
    /// verbatim (processor.cpp's setState body): pick a buffer that is neither
    /// published nor being consumed, seed the WHOLE buffer from
    /// spectralSlotsAuthoring_ so the three unedited slots are not lost, install
    /// @p edited, and publish with a release store ONLY if the result is valid.
    ///
    /// @param edited The already-mutated copy of spectralSlotsAuthoring_[slot].
    ///        The mutation is performed by the caller rather than by a
    ///        callback so this stays a plain (non-template) member function; the
    ///        seed-mutate-publish order plan 6.2 specifies is unchanged, because
    ///        mutating a copy of the mirror and mutating the freshly-seeded
    ///        staging slot are the same operation on the same bytes.
    ///
    /// SpectralMorphEngine::setState rejects an invalid state wholesale
    /// (spectral_morph_engine.h:296-298), so publishing one would be a SILENTLY
    /// INERT edit; the validity gate here turns it into a dropped one, which is
    /// at least consistent with what the ring contains.
    void stageSlotEdit(std::size_t slot, const Krate::DSP::SpectralState& edited) noexcept;

    /// Plan 6.2a. Bring spectralSlotsAuthoring_ back in step with the 409-412
    /// slot dropdowns, MESSAGE-THREAD ONLY.
    ///
    /// WHY IT IS NOT DONE IN refreshSpectralSlotFromFactory(). That function runs
    /// from processParameterChanges(), i.e. ON THE AUDIO THREAD (it writes
    /// spectralSlots_, the audio-thread-owned array). Writing the
    /// message-thread-only mirror from there would be exactly the cross-thread
    /// write the mirror exists to avoid. Instead the mirror is reconciled lazily,
    /// on the message thread, at the two points that read it - stageSlotEdit()
    /// and getState() - by comparing the pack's atomic against a
    /// message-thread-owned copy of what the mirror was last seeded from.
    void syncAuthoringMirrorFromDropdowns() noexcept;
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
    /// half, NOT by its per-base loop.
    [[nodiscard]] bool anyMacroSmootherUnsettled() const noexcept;
    /// PER-TARGET, never one flag for all kNumTargets (plan 3.5.5). A single
    /// global flag would push every base on every chunk of a settling window -
    /// 27 x 26 increments of setTargetBasePushes_ - and falsify SC-007's own
    /// table. Answers only for the 27 targets pushMacroSurfaces() owns; the two
    /// Effects-owned rows are pushEffectsMacroBases()', which has no settling
    /// clause (its bases are raw atomics, not smoother read-backs).
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

    /// Phase 11 C-10 / FR-037..FR-039 (plan section 4). The macro-composed
    /// effects targets for THIS process() call: `deep base + the macro rows`,
    /// computed once in the pre-slice block and read by all three consumers
    /// (updateEffectsBypassState(), setParamSmootherTargets() and FR-010's
    /// ENGAGE predicate) INSTEAD of the raw ID 1410 / 1441 atomics.
    ///
    /// It is a plain member and not an atomic: only the audio thread writes or
    /// reads it. Assignment is a two-float copy.
    ///
    /// FR-039 is arithmetic, not a special case: at the FR-060 macro neutrals
    /// every row contributes exactly 0 (seraphis_macro_matrix.h:884-889) and
    /// baseValueForTarget() maps both targets onto the deep atomic, so the
    /// composed value is bit-identical to the atomic it replaced.
    Krate::DSP::SeraphisEffectsTargets composedEffects_{};
    /// FR-012's cadence counter for the composition above - once per CALL.
    std::size_t composedEffectsRecomputes_ = 0;
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

    // --- Phase 11 C-2: the cloud-frame producer ------------------------------
    // Built in connect(), released in disconnect(); the queue is opened and
    // closed by setActive(). NULL in every plugin-side test, because
    // ProcessorFixture never calls connect() - which is exactly why
    // publishCloudFrame() does not treat it as a precondition.
    std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_;

    /// C-2 clause 6's gate, written from Processor::notify on the MESSAGE
    /// thread (kind 0, T010) and from setCloudFrameGateForTest, read here on the
    /// AUDIO thread every process() call. std::atomic<bool>, never a plain bool
    /// (plan D-6): an unsynchronised cross-thread bool is a data race under the
    /// C++ memory model however benign the codegen looks. `relaxed` is
    /// sufficient - the flag publishes no other state.
    std::atomic<bool> cloudFrameEnabled_{false};

    /// The frame under construction. A MEMBER, not a stack local: it is
    /// std::memset to zero ONCE in setupProcessing() and only field-assigned
    /// thereafter, so CloudFrame's 4 interior padding bytes are deterministically
    /// zero in every published block (a stack local zero-initialised per call
    /// would also work, but adds an 808-byte memset to every process() call).
    CloudFrame pendingFrame_{};
    std::uint32_t cloudFrameSequence_ = 0;  // monotonic; wrap is benign

    // C-2 clause 7's TWO counters. They count different things and no criterion
    // conflates them - see the two accessors above.
    std::size_t cloudFramePublishAttempts_ = 0;
    std::size_t cloudFrameSkippedBlocks_ = 0;
    std::size_t renderSlices_ = 0;  // SC-007's strict `>` divisor

    // SC-009(b) stage instrumentation. Same shape as Phase 10's trio, and OFF on
    // every shipping path for the same Constitution II reason.
    bool cloudFrameInstrumented_ = false;
    double cloudFrameStageNs_ = 0.0;
    std::size_t cloudFrameStageProcessCalls_ = 0;

    // Phase 11.5 Step 0 decomposition instrumentation - see the DecompStage
    // banner above. OFF on every shipping path (Constitution II).
    bool processDecompInstrumented_ = false;
    std::array<double, static_cast<std::size_t>(DecompStage::Count)> decompNs_{};
    std::size_t decompProcessCalls_ = 0;

    /// The previous focus slot, C-2 clause 4(b)'s retention state. Audio-thread
    /// owned; only publishCloudFrame() reads or writes it.
    std::size_t cloudFrameFocusVoice_ = 0;

    /// FR-030's override table, the half publishCloudFrame() MIRRORS.
    /// Bit i set <=> partial i is masked / carries an authored pan. The message
    /// thread writes them (C-5 kinds 2 and 3, T010) and the audio thread reads
    /// them, so both are atomic; `relaxed` is correct because the ordering that
    /// matters is partialOverridesPending_'s release/acquire handshake, not
    /// these loads. T010 added the other two members (partialPanStaging_ and
    /// partialOverridesPending_, just below) with the kinds 2/3 dispatch that
    /// writes them; T011 added the CONSUMER, repushPartialOverrides().
    std::atomic<std::uint64_t> partialMaskBits_{0};
    std::atomic<std::uint64_t> partialPanOverrideBits_{0};

    /// C-5 kind 2's staged pan values, one per partial. The message thread
    /// writes, the audio thread reads (T011's repushPartialOverrides()), so
    /// every entry is atomic; `relaxed` is correct because the ordering that
    /// matters is partialOverridesPending_'s release/acquire handshake.
    std::array<std::atomic<float>, Krate::DSP::HarmonicCloud::kMaxPartials> partialPanStaging_{};
    /// The handshake itself. T010 is the PRODUCER (kinds 2 and 3 store true with
    /// release); T011 added the CONSUMER - one
    /// `exchange(false, std::memory_order_acquire)` per process() call, beside
    /// pushSpectralStatesIfPending(). The acquire on that exchange is what makes
    /// the relaxed pan/mask writes above visible to repushPartialOverrides()
    /// before it reads them; consume-and-clear is what makes an edit that arrives
    /// DURING a fan-out picked up next call rather than lost or applied twice.
    std::atomic<bool> partialOverridesPending_{false};

    /// Phase 11 FR-030's stereo-spread clearing event, keyed on the COMPOSED
    /// value and NEVER on ParamID 207.
    ///
    /// SeraphisMacroMatrix::apply() pushes CloudStereoSpread through
    /// SeraphisVoice::setStereoSpread on EVERY slice, and HarmonicCloud::
    /// setStereoSpread wipes positionOverridden_ on any VALUE change
    /// (harmonic_cloud.h:535-547). A tracker keyed on the ParamID is blind to the
    /// macro half - Bloom writes that target with base 0.35 / amount 0.60
    /// (seraphis_macro_matrix.h) - so a macro-ring sweep with the deep knob held
    /// still would silently drop every authored pan (SC-014 arm 6).
    ///
    /// The tracked quantity is the value the cloud ACTUALLY STORED, read back
    /// through HarmonicCloud::getStereoSpread() (:549) immediately after
    /// macros_.apply(). That is exact rather than approximate: setStereoSpread
    /// clears the overrides IFF its post-clamp value differs from the stored one,
    /// so comparing the stored value across slices detects exactly the clearing
    /// events and nothing else. (SeraphisMacroMatrix::at()/evaluateAll() are
    /// private, so the composed value cannot be read from the matrix directly -
    /// and the read-back is the stronger observable anyway.)
    /// The EXTENT of apply()'s loop, tracked beside its value. apply() writes
    /// voices [0, getPolyphony()) only (seraphis_macro_matrix.h:712-715), so a
    /// polyphony INCREASE hands the composed spread to slots that never received
    /// it - clearing their positionOverridden_ - while voice 0's stored value,
    /// the read-back above, never moves. The value alone is therefore blind to
    /// exactly the slots that were cleared; the pair is not.
    float lastPushedComposedSpread_ = 0.0f;
    std::size_t lastPushedSpreadVoiceCount_ = 0u;
    bool lastPushedComposedSpreadValid_ = false;

    // --- Phase 11 C-5 (plan 6.2/6.2a, T010): the MESSAGE-THREAD edit state ---
    //
    // Not one of these is read or written by process(). They are the second
    // writer on the staging ring Phase 9 built, and they are deliberately NOT
    // atomic: the message thread owns them outright.

    /// Plan 6.2a. The message-thread mirror of the four morph slots.
    /// spectralSlots_ is AUDIO-THREAD-OWNED (see its banner above, and
    /// getState()'s "IT NEVER READS spectralSlots_"), so stageSlotEdit() may not
    /// read it either. Seeded in the constructor from the registered slot
    /// defaults, reconciled with the 409-412 dropdowns by
    /// syncAuthoringMirrorFromDropdowns(), overwritten wholesale by setState(),
    /// and mutated in place by every ACCEPTED stageSlotEdit().
    std::array<Krate::DSP::SpectralState, 4> spectralSlotsAuthoring_{};
    /// What spectralSlotsAuthoring_[s] was last seeded from. Message-thread
    /// owned; -1 is never a legal factory index, so a fresh processor reconciles
    /// on its first read. It is NOT lastPushedSlotStateId_ - that one is the
    /// AUDIO thread's tracker for the same dropdowns, and sharing it would be a
    /// cross-thread write.
    std::array<int, 4> lastAuthoredSlotStateId_{-1, -1, -1, -1};

    /// Q2. The pristine "A" a Blend gesture re-blends from, latched by kind 7 and
    /// read by every kind 4 until the gesture ends. This is what makes Blend
    /// ABSOLUTE rather than compounding.
    Krate::DSP::SpectralState blendSnapshotA_{};
    bool blendSnapshotValid_ = false;
    /// C-5 kind 6. UI session state, never a ParamID (Non-goals) and never
    /// serialized.
    int selectedEditSlot_ = 0;
    /// SC-025's seam: accepted-and-published staging writes from the edit
    /// channel. Message-thread counter, never reset.
    std::size_t editStageWrites_ = 0;

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
