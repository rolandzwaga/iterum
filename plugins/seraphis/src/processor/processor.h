#pragma once

// ==============================================================================
// Seraphis - Audio Processor (audio thread)
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation.
// This header NEVER includes anything under controller/.
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"

#include "parameters/aether_params.h"
#include "parameters/atmosphere_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/global_params.h"
#include "parameters/life_mod_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"

#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
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

private:
    // FR-059a. The ONE seam SC-005's positive control (b) needs. Declared above
    // this class and defined ONLY by the SC-005 test TU, so no shipping build
    // can name it, let alone call it.
    friend struct detail::SeraphisParamSmootherBypassProbe;

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushGlobalParams() noexcept;                                    // FR-024 step 0 / FR-024a
    void renderSlice(float* outL, float* outR, std::size_t n) noexcept;  // FR-024 steps 2-6

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
    /// Nine setTarget() calls (smoother.h:170) from the already-clamped atomics.
    /// Called ONCE per process(), from the plan 3.3 pre-slice block, BEFORE the
    /// slice loop reads anyClassBSmootherUnsettled(). It deliberately does NOT
    /// consume snapParamSmoothers_ - advanceParamSmoothers() does, because the
    /// snap has to outlive the target set that precedes it.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    void setParamSmootherTargets() noexcept;

    /// Advance (or snap) all nine by the SUB-SLICE's own sample count, so the
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
    ///      Returns a FIXED-SIZE array BY VALUE - nine raw pointers to members
    ///      of `*this`, one per class-(b) ID (plan 3.5.3). The return type is
    ///      PINNED here because this is the hottest new audio-thread path in
    ///      the phase (twice per sub-slice, up to 32 times per 2048-sample
    ///      block while settling) and a range-returning helper with an unstated
    ///      type is where an implementer would reach for std::vector and
    ///      allocate per sub-slice. SC-006 would catch that, but only after the
    ///      fact and only if the settling window overlaps the measured render.
    [[nodiscard]] std::array<Krate::DSP::OnePoleSmoother*, 9> classBSmoothers() noexcept;

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
    bool lastPushedSoftLimitValid_ = false;

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

    // --- FR-059(b): the nine class-(b) smoothers (plan 3.5.3) -----------------
    // Class (b) is EXACTLY nine IDs: 100, 101, 102, 103, 104, 801, 802, 1215,
    // 1216. Each is seeded with its own registered default so the very first
    // render starts from the shipped value rather than ramping up from 0 (the
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
    bool lastPushedSoftLimit_ = true;      // FR-044 on-change tracker
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
