// ==============================================================================
// Layer 3: System - SeraphisVoice (Seraphis single-voice chain)
// ==============================================================================
// Seraphis Phase 7. Spec slug: seraphis-phase7-voice-engine.
//   Spec:    specs/seraphis-phase7-voice-engine/spec.md
//   Plan:    specs/seraphis-phase7-voice-engine/plan.md   (§2)
//   Roadmap: specs/Seraphis-roadmap.md, Part A -> Phase 7 (lines 288-315)
//
// One playable voice: harmonic cloud -> voice envelope -> continuous body ->
// atmosphere tap -> spatial position (FR-010; the envelope sits on the
// EXCITATION path, pre-body, so the body's decay cloud and the atmosphere's
// grains ring out after the gate closes). Composes the Phase 2/3/4/5 systems
// that already exist as siblings in this directory; adds no new DSP of its own
// beyond the envelope/spatial/level stages described in plan §2.
//
// LAYER DISCIPLINE (FR-001). Layer 3 may include Layers 0-2 plus its Layer 3
//   PEERS. This header includes core/, primitives/, processors/ and the four
//   systems/ peers it owns, and NEVER an effects/ (Layer 4) header. The reverb
//   lives outside the voice by construction - see seraphis_engine.h.
//   Every Layer 0/1/2 include below is spelled out BY NAME rather than left to
//   a transitive path: `EnvCurve` (core/env_curve.h:24) and `RetriggerMode`
//   (primitives/envelope_utils.h:64) live in two DIFFERENT headers, and
//   `EnvCurve` would otherwise reach this TU only through
//   processors/multi_stage_envelope.h - exactly the kind of transitive
//   dependency that breaks on a sibling refactor.
//   node tools/lint-layers.js gates this, not inspection.
//
// REAL-TIME SAFETY CONTRACT (FR-003). prepare() is the ONLY method that
//   allocates and the only one that is not real-time safe. Everything else is
//   noexcept, allocation-free, lock-free, exception-free and I/O-free.
//
// NON-FINITE POLICY. Bit-pattern checks only (isFiniteBits, the
//   continuous_body.h:1346-1351 shape). std::isnan / std::isinf / std::isfinite
//   appear NOWHERE in this header: the macOS leg builds with -ffast-math, which
//   licenses the compiler to fold them away.
//
// THERE IS NO getLatencySamples() ON THIS CLASS, AND THAT IS A DECISION
//   (FR-015). The voice adds 0 samples of latency: plan §1 D1's carry FIFO
//   renders a control chunk on demand at the moment its first sample is asked
//   for, so nothing is produced ahead of the caller's position. The
//   sub-components that DO report latency (the atmosphere's blur stage) are
//   parallel texture, not a delay in the dry path.
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/env_curve.h>
#include <krate/dsp/core/random.h>

// Layer 1: Primitives
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/primitives/smoother.h>

// Layer 2: Processors
#include <krate/dsp/processors/growth_envelope.h>
#include <krate/dsp/processors/midside_processor.h>
#include <krate/dsp/processors/multi_stage_envelope.h>
#include <krate/dsp/processors/orbit_modulator.h>
#include <krate/dsp/processors/spectral_state.h>

// Layer 3: Systems (peers)
#include <krate/dsp/systems/atmosphere_engine.h>
#include <krate/dsp/systems/continuous_body.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/spectral_morph_engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Krate {
namespace DSP {

namespace detail {
/// @brief SC-003 positive control (b). DECLARED HERE, DEFINED ONLY BY A TEST TU.
///
/// SC-003 requires TWO positive controls, and the second is a build in which
/// FR-034's silence() ramp is replaced by a hard cut (`kSilenceRampMs = 0`),
/// which must FAIL clause 3. `kSilenceRampMs` is a compile-time constant and
/// `silenceRampSamples_` is private, so without this the control could only be
/// produced by editing the header by hand - i.e. it could never be run by CI and
/// its measured figure could never be kept honest.
///
/// It costs nothing at run time and adds no public surface: the library never
/// defines it, so a shipping build has no way to call it. Exactly the shape
/// `detail::SeraphisEngineNonFiniteProbe` (seraphis_engine.h:113) already uses
/// for the same reason, which in turn follows AetherReverb's
/// `injectNonFiniteStateForTest` (aether_reverb.h:2691-2722).
///
/// ODR: swept this session - `SeraphisVoiceSilenceRampProbe` has zero matches in
/// dsp/ or plugins/.
struct SeraphisVoiceSilenceRampProbe;
}  // namespace detail

/// @brief Per-voice construction options (FR-004).
///
/// Forwarded verbatim from SeraphisEngineConfig; every field is consumed by
/// prepare() and nothing here is settable afterwards. Every field is CLAMPED,
/// never rejected - a hostile config still produces a usable voice.
struct SeraphisVoiceConfig {
    /// FR-014. Rolling-capture length in seconds; clamped into AtmosphereEngine's
    /// supported [1, 30] range by prepare().
    float captureSeconds = 4.0f;
    /// Allocate the atmosphere blur STFT stage.
    bool blurEnabled = true;
    /// Allocate the atmosphere freeze STFT stage.
    bool freezeEnabled = true;
    /// Blur STFT size.
    std::size_t blurFftSize = 1024;
    /// Freeze STFT size.
    std::size_t freezeFftSize = 2048;
    /// Largest block processStereoBlock() may be handed; clamped to
    /// [1, kMaxBlockSamples].
    std::size_t maxBlockSamples = 2048;
};

/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
/// @par Real-Time Safety: everything except prepare() is noexcept, allocation-free,
///      lock-free.
class SeraphisVoice {
public:
    // =========================================================================
    // Nested types - CLASS-SCOPED on purpose (plan §0.2)
    // =========================================================================

    /// FR-021. Class-scoped because `EnvelopeMode` is generic enough that a
    /// future component could collide with it at namespace scope; the
    /// `HarmonicCloud::kMaxPartials` precedent (harmonic_cloud.h:132-138) is the
    /// same reasoning.
    enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 };

    // =========================================================================
    // Constants - ALL class-scoped (plan §0.2, §2.1)
    // =========================================================================

    /// FR-007. Matches harmonic_cloud.h:144, continuous_body.h:97 and
    /// atmosphere_engine.h:269 - the shared control-rate grid.
    static constexpr std::size_t kControlChunkSamples = 64;
    /// FR-004. Matches atmosphere_engine.h:373's default maximum block.
    static constexpr std::size_t kMaxBlockSamples = 2048;
    /// FR-032. -100 dBFS.
    static constexpr float kTailSilenceThreshold = 1.0e-5f;
    /// FR-033. Level-detector release time constant TAU (plan D2 - NOT a
    /// time-to-99%, which is why calculateOnePolCoefficient must not be used).
    static constexpr float kLevelReleaseMs = 100.0f;
    /// FR-034. silence() ramp length. Deliberately shorter than one control
    /// chunk at every supported rate, so a steal completes inside one chunk.
    static constexpr float kSilenceRampMs = 1.0f;
    /// FR-032. Consecutive quiescent control chunks before the voice retires.
    static constexpr int kQuiescentChunksToRetire = 4;
    /// FR-025. Per-voice M/S width bounds, in percent.
    static constexpr float kMinVoiceWidthPct = 50.0f;
    static constexpr float kMaxVoiceWidthPct = 150.0f;
    /// FR-025. Azimuth-balance smoothing time.
    static constexpr float kSpatialSmoothMs = 20.0f;
    /// FR-025. Unity-at-centre constant-power constant.
    static constexpr float kSqrt2 = 1.41421356f;
    /// FR-020. The ONE curve every envelope setStage() call uses. `EnvCurve` is
    /// core/env_curve.h:24 (Layer 0) - NOT primitives/envelope_utils.h, which
    /// declares RetriggerMode (:64) and no curve enum.
    static constexpr EnvCurve kStageCurve = EnvCurve::Exponential;
    /// FR-020. Stage count the voice authors. `MultiStageEnvelope::kMinStages`
    /// is 4 (multi_stage_envelope.h:63) and the voice uses exactly that.
    static constexpr int kEnvelopeStages = 4;
    /// FR-020. Sustain point; stages 0..1 are the pre-sustain walk.
    static constexpr int kEnvelopeSustainPoint = 2;
    /// FR-025. Half-range the orbit's y axis sweeps around the width CENTRE.
    /// Fixed, and deliberately NOT macro-writable (plan §2.6).
    static constexpr float kVoiceWidthSpanPct = 50.0f;

    // --- seed salts (FR-016; pairwise distinct, non-overlapping ranges) ------
    static constexpr std::size_t kCloudSalt = 0x0100;
    static constexpr std::size_t kMorphSalt = 0x0200;
    static constexpr std::size_t kBodySalt = 0x0300;
    static constexpr std::size_t kAtmosSalt = 0x0400;
    static constexpr std::size_t kOrbitSalt = 0x0500;
    static_assert(kCloudSalt != kMorphSalt && kMorphSalt != kBodySalt && kBodySalt != kAtmosSalt
                      && kAtmosSalt != kOrbitSalt,
                  "FR-016");

    /// FR-002 / FR-013 coarse ownership guard, asserted just below the class.
    ///
    /// The eight declared sub-components, in declaration order, are:
    ///   HarmonicCloud, SpectralMorphEngine, ContinuousBody, AtmosphereEngine,
    ///   MultiStageEnvelope, GrowthEnvelope, OrbitModulator, MidSideProcessor
    /// plus 8 x 64 floats of scratch (2 048 B, plan §10 V-1) and a handful of
    /// scalars. Any of the six members FR-002 forbids - StereoField,
    /// VoiceModRouter, ModulationEngine, PolySynthEngine, SynthVoice,
    /// AetherReverb - would blow this bound.
    ///
    /// MEASURED, not guessed: `sizeof(SeraphisVoice)` is **47 616 B** (g++ 13.3,
    /// -std=c++20, -O1, repo headers, this class as shipped; plan §3.2's
    /// per-member arithmetic predicted 47 380 B). The bound is
    /// ceil(47 616 x 1.05) = 49 997 rounded up to the next 64 B, so a padding
    /// difference between toolchains does not turn a size guard into a build
    /// break while any forbidden member still blows it.
    static constexpr std::size_t kVoiceSizeBound = 50048;

    // =========================================================================
    // Construction
    // =========================================================================

    SeraphisVoice() noexcept = default;

    // NON-COPYABLE AND NON-MOVABLE, stated rather than silently produced.
    // `ContinuousBody` user-declares a deleted copy constructor and NO move
    // members (continuous_body.h:647-648), which suppresses its implicit move;
    // overload resolution then selects the deleted copy ctor. `= default`ed move
    // members here would therefore be DEFINED AS DELETED while reading as if the
    // type were movable. Nothing in Phase 7 moves a voice -
    // std::array<SeraphisVoice, kMaxVoices> does not require it.
    SeraphisVoice(const SeraphisVoice&) = delete;
    SeraphisVoice& operator=(const SeraphisVoice&) = delete;
    SeraphisVoice(SeraphisVoice&&) = delete;
    SeraphisVoice& operator=(SeraphisVoice&&) = delete;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief FR-003. The ONLY allocating path; not real-time safe.
    ///
    /// Order is plan §2.3 and is load-bearing throughout. Calling it a second
    /// time fully reconfigures. Every `cfg` field is clamped, never rejected
    /// (FR-004). Ends with reset(), so a freshly prepared voice is silent.
    void prepare(double sampleRate, const SeraphisVoiceConfig& cfg) noexcept {
        // --- 1. rate floor (the atmosphere_engine.h:406-408 idiom) -----------
        sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0;

        // --- 2. clamp the config; NEVER reject (FR-004) ----------------------
        const float captureRequest =
            isFiniteBits(cfg.captureSeconds) ? cfg.captureSeconds : 4.0f;
        const float captureSeconds = std::clamp(captureRequest,
                                                AtmosphereEngine::kMinCaptureSeconds,
                                                AtmosphereEngine::kMaxCaptureSeconds);
        const std::size_t blurFftSize = std::clamp(cfg.blurFftSize,
                                                   AtmosphereEngine::kMinBlurFftSize,
                                                   AtmosphereEngine::kMaxBlurFftSize);
        const std::size_t freezeFftSize = std::clamp(cfg.freezeFftSize,
                                                     AtmosphereEngine::kMinFreezeFftSize,
                                                     AtmosphereEngine::kMaxFreezeFftSize);
        const std::size_t maxBlockSamples =
            std::clamp(cfg.maxBlockSamples, std::size_t{1}, kMaxBlockSamples);

        // --- 3. sub-component prepare ----------------------------------------
        cloud_.prepare(sampleRate_);                       // harmonic_cloud.h:282
        morph_.prepare(sampleRate_);                       // spectral_morph_engine.h:231
        body_.prepare(sampleRate_);                        // continuous_body.h:660
        // Designated initialisers only - no narrowing in brace init.
        atmos_.prepare(sampleRate_,                        // atmosphere_engine.h:405
                       AtmosphereEngine::PrepareConfig{.captureSeconds = captureSeconds,
                                                       .blurEnabled = cfg.blurEnabled,
                                                       .freezeEnabled = cfg.freezeEnabled,
                                                       .blurFftSize = blurFftSize,
                                                       .freezeFftSize = freezeFftSize,
                                                       .maxBlockSamples = maxBlockSamples});
        mse_.prepare(static_cast<float>(sampleRate_));     // :73 takes FLOAT, not double
        growth_.prepare(sampleRate_);                      // growth_envelope.h:117
        orbit_.prepare(sampleRate_);                       // orbit_modulator.h:137
        ms_.prepare(static_cast<float>(sampleRate_),       // midside_processor.h:96
                    kControlChunkSamples);

        // --- 4. seeds, BEFORE the first note ---------------------------------
        // ContinuousBody::setSeed is "configure-time only, and deliberately NOT
        // retro-deterministic" (continuous_body.h:1117-1124).
        applySeeds();

        // --- 5. FR-019a: the ONLY place setState/setStateCount are called -----
        // The morph engine's own ctor loads all four slots with SineStack, under
        // which Bloom's setTargetPosition row would morph SineStack -> SineStack
        // and be inaudible.
        morph_.setState(0, makeFactoryState(SpectralStateId::SineStack));
        morph_.setState(1, makeFactoryState(SpectralStateId::Glass));
        morph_.setStateCount(2);
        // Fully qualified: TravelMode is nested at spectral_morph_engine.h:139.
        morph_.setTravelMode(SpectralMorphEngine::TravelMode::External);
        morph_.setTargetPosition(0.0f);

        // --- 6. FR-019 shipped voice defaults --------------------------------
        // The complete table, including every (unchanged) row, so the table IS
        // the code. Rows marked (unchanged) adopt the component default as a
        // DECISION; see spec FR-019 for the per-row reason.

        // Harmonic cloud
        cloud_.setRichness(0.60f);          // component 1.0 (its clamp max)
        cloud_.setInharmonicity(0.030f);    // component 0.0 (its floor)
        cloud_.setSpectralTiltDb(0.0f);     // (unchanged)
        cloud_.setMutation(0.25f);          // component 0.0
        cloud_.setSpectralGravity(0.20f);   // component 0.0
        cloud_.setDriftDepthCents(0.0f);    // (unchanged)
        cloud_.setStereoSpread(0.35f);      // component 0.0
        cloud_.setAttackTimeSec(0.05f);     // (unchanged) - the component floor
        cloud_.setDecayTimeSec(0.5f);       // (unchanged)

        // Spectral morph
        morph_.setEntropy(0.20f);           // component 0.0
        morph_.setBloom(0.0f);              // (unchanged)
        morph_.setTravelRate(SpectralMorphEngine::kMinTravelRate);  // (unchanged) = 1/600

        // Continuous body
        body_.setMaterial(ContinuousBody::BodyMaterial::Glass);  // (unchanged)
        body_.setResonance(0.7f);           // (unchanged)
        body_.setDamping(0.25f);            // component 0.0 == its floor: zero-travel fix
        body_.setKeyTracking(1.0f);         // (unchanged)
        body_.setDrive(1.0f);               // (unchanged)
        body_.setMix(1.0f);                 // (unchanged) - fully wet, cloud reaches the
                                            //               output only through the resonators
        body_.setCloudMix(0.25f);           // (unchanged)
        body_.setCloudDecaySec(4.0f);       // (unchanged)
        body_.setCloudSize(1.0f);           // (unchanged)
        body_.setCloudDamping(0.3f);        // (unchanged)
        body_.setWidth(1.0f);               // (unchanged)

        // Atmosphere
        atmos_.setLevel(0.5f);              // component 1.0
        atmos_.setBlur(0.0f);               // (unchanged)
        atmos_.setDensity(4.0f);            // (unchanged)
        atmos_.setGrainSeconds(4.0f);       // (unchanged) - equals FR-014's capture ring
        atmos_.setDriftDepth(0.3f);         // (unchanged)
        atmos_.setPanSpread(0.7f);          // (unchanged)
        atmos_.setDecorrelation(0.5f);      // (unchanged)
        atmos_.setFreezeMix(0.0f);          // (unchanged); freeze is NOT captured

        // Spatial
        orbit_.setDepth(0.35f);             // component 1.0 == its clamp max: zero-travel fix
        orbit_.setRate(0.1f);               // (unchanged) - one orbit per 10 s
        orbit_.setCoupling(0.0f);           // (unchanged) - voices must drift independently
        orbit_.setGrowth(0.0f);             // (unchanged) - the documented sustain neutral
        setVoiceWidthBasePercent(100.0f);   // FR-062's VoiceWidth base; at 100 the §2.6
                                            // expression reduces to FR-025's 100 + y*50

        // --- 7. FR-020 envelope defaults -------------------------------------
        // Mode first: applyStage's Growth branch zeroes pre-sustain times, and a
        // prepare() issued while the voice was in Growth mode must still author
        // the Standard shape.
        envMode_ = EnvelopeMode::Standard;
        mse_.setNumStages(kEnvelopeStages);           // multi_stage_envelope.h:135
        mse_.setSustainPoint(kEnvelopeSustainPoint);  // :178 - read by applyStage
        // applyStage's FR-059 idempotence guard compares against the shadows, so
        // the four stages this prepare authors are invalidated first. Without
        // this, (a) stage 3's {0, 0} would match the zero-initialised shadow and
        // never reach mse_, and (b) a re-prepare after a Growth-mode session
        // would find the shadows already equal and silently leave mse_ holding
        // the 0 ms pre-sustain times.
        for (int st = 0; st < kEnvelopeStages; ++st) {
            const auto i = static_cast<std::size_t>(st);
            stageLevel_[i] = -1.0f;
            stageTimeMs_[i] = -1.0f;
        }
        applyStage(0, 1.0f, 2000.0f);   // attack
        applyStage(1, 0.7f, 4000.0f);   // decay
        applyStage(2, 0.7f, 0.0f);      // sustain hold
        applyStage(3, 0.0f, 0.0f);      // post-sustain
        releaseMs_ = 8000.0f;
        mse_.setReleaseTime(releaseMs_);                  // :206
        // EXPLICIT: the component default is RetriggerMode::Hard (:463), which
        // would restart a 2 s attack on every re-articulation.
        mse_.setRetriggerMode(RetriggerMode::Legato);     // :215
        growth_.setDuration(10.0f);                       // growth_envelope.h:144

        // --- 8. derived constants --------------------------------------------
        // Plan D2: NOT calculateOnePolCoefficient, which treats its argument as
        // time-to-99% = 5*tau (smoother.h:86-88) and would give tau = 20 ms
        // instead of 100 ms, breaking FR-032's 1.15 s retirement derivation.
        levelReleaseCoeff_ = std::exp(-static_cast<float>(kControlChunkSamples)
                                      / (0.001f * kLevelReleaseMs
                                         * static_cast<float>(sampleRate_)));
        silenceRampSamples_ = std::max(
            1, static_cast<int>(std::lround(0.001f * kSilenceRampMs
                                            * static_cast<float>(sampleRate_))));
        // The cast is required: configure() takes a FLOAT sample rate and MSVC
        // raises C4244 without it.
        gainLSm_.configure(kSpatialSmoothMs, static_cast<float>(sampleRate_));
        gainRSm_.configure(kSpatialSmoothMs, static_cast<float>(sampleRate_));

        // --- 9. -------------------------------------------------------------
        prepared_ = true;
        reset();
    }

    /// @brief FR-005. Full reset; CLEARS the silence fade tail (plan D3).
    ///
    /// Restores RUN state only. Parameters are NOT restored - that is the owned
    /// sub-components' own reset() contract ("Clear all internal state; leave
    /// every parameter unchanged", continuous_body.h:757), so the voice inherits
    /// it rather than inventing a different one.
    void reset() noexcept {
        resetRunState();
        fadeTailL_ = 0.0f;
        fadeTailR_ = 0.0f;
        fadeRemaining_ = 0;
    }

    /// @brief FR-047. Steal-time reset; PRESERVES the armed fade tail (plan D3).
    ///
    /// Identical to reset() except that the anti-click tail silence() armed
    /// survives into the new note. The ONLY caller is SeraphisEngine's steal /
    /// orphan-slot teardown; SeraphisEngine::silence() uses reset() so FR-055's
    /// "the next block is exactly 0" holds.
    void resetForSteal() noexcept { resetRunState(); }

    /// @brief FR-034. Arm a short anti-click decay, then HARD-CLEAR every
    ///        sub-component.
    ///
    /// silence() cannot fade by rendering: a steal is issued between blocks, so
    /// there are no samples for a fade to occupy (plan D3). The discontinuity is
    /// the last emitted sample pair followed by the new note's ~0 onset, so the
    /// pair is captured and added, decaying, to the first silenceRampSamples_
    /// samples rendered afterwards.
    ///
    /// atmos_.reset() and NOT atmos_.silence(): the latter only sets
    /// runState_ = Silencing (atmosphere_engine.h:644-650) and keeps rendering
    /// the grain bed under a 10 ms ramp (:278) - ten times this class's own
    /// ramp. reset() is the class's only immediate clear.
    /// orbit_ and ms_ are NOT cleared: they are life state and produce no signal
    /// of their own (FR-051/SC-016 require the orbit to keep advancing).
    void silence() noexcept {
        fadeTailL_ = lastOutL_;
        fadeTailR_ = lastOutR_;
        fadeRemaining_ = silenceRampSamples_;
        cloud_.reset();   // harmonic_cloud.h:313
        morph_.reset();   // spectral_morph_engine.h:249
        body_.reset();    // continuous_body.h:766
        mse_.reset();     // multi_stage_envelope.h:79
        growth_.reset();  // growth_envelope.h:129
        atmos_.reset();   // atmosphere_engine.h:525 - a HARD clear
        // Discard any un-served rendered audio.
        carryAvail_ = 0;
        carryRead_ = 0;
        carryIsLifeOnly_ = true;
    }

    /// @brief FR-006. Render `n` samples into the caller's stereo buffers.
    ///
    /// THE VOICE NEVER RENDERS A PARTIAL CHUNK (plan §1 D1). HarmonicCloud,
    /// SpectralMorphEngine and EntropyProcessor each take exactly ONE control
    /// step per call regardless of the length passed
    /// (harmonic_cloud.h:908-912, spectral_morph_engine.h:405-412), so passing
    /// sub-chunks down would give a 36 + 28 split two control steps where an
    /// unsplit 64 gives one. Whole chunks are rendered into a 64-sample stereo
    /// carry FIFO and the caller is served out of it, which costs zero added
    /// latency (a chunk is rendered on demand at the moment its first sample is
    /// requested) and makes FR-007's partition invariance exact.
    void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept {
        // Guard order mirrors continuous_body.h:1166-1180.
        if (outL == nullptr || outR == nullptr) {
            return;  // a null pointer means NOTHING is written
        }
        if (n == 0) {
            return;  // consumes no control step
        }
        if (!prepared_) {
            std::fill_n(outL, n, 0.0f);
            std::fill_n(outR, n, 0.0f);
            return;
        }

        std::size_t done = 0;
        while (done < n) {
            if (carryAvail_ == 0) {
                renderOneChunk();  // always exactly kControlChunkSamples
            }
            const std::size_t take = std::min(n - done, carryAvail_);
            std::copy_n(carryL_.data() + carryRead_, take, outL + done);
            std::copy_n(carryR_.data() + carryRead_, take, outR + done);
            // Plan D3: the anti-click tail is armed from the last sample the
            // caller ACTUALLY RECEIVED, captured here at SERVE time - NOT from
            // carryL_[63] at render time, which on a mid-chunk steal is up to 63
            // samples of program material away from the amplitude the output
            // actually reached.
            lastOutL_ = outL[done + take - 1];
            lastOutR_ = outR[done + take - 1];
            carryRead_ += take;
            carryAvail_ -= take;
            done += take;
        }
    }

    /// @brief FR-027. Advance the life modulators for `n` samples without
    ///        rendering, on the SAME carry clock processStereoBlock uses.
    ///
    /// Follows HarmonicCloud's quiescent early-out idiom, which still advances
    /// its drift lanes so "a silent render and a sounding render of the same
    /// length leave identical lane state" (harmonic_cloud.h:893-903).
    void advanceLifeOnly(std::size_t n) noexcept {
        if (n == 0 || !prepared_) {
            return;
        }
        std::size_t done = 0;
        while (done < n) {
            if (carryAvail_ == 0) {
                advanceOneChunkLifeOnly();
            }
            const std::size_t take = std::min(n - done, carryAvail_);
            carryRead_ += take;
            carryAvail_ -= take;
            done += take;
        }
    }

    // =========================================================================
    // Notes
    // =========================================================================

    /// FR-023. Retune the body and the cloud, gate the envelope.
    /// The two frequency clamps differ deliberately: ContinuousBody clamps to
    /// [20, 8000] (continuous_body.h:118-119) and HarmonicCloud to [20, 4000]
    /// (harmonic_cloud.h:184-185), so MIDI 127 lands at different places in the
    /// two engines. Documented, not repaired.
    void noteOn(float frequencyHz, float velocity) noexcept {
        hasSounded_ = true;           // §2.11 configure-time gate closes here
        renderedSinceNoteOn_ = false; // plan D4 rule 2
        if (carryIsLifeOnly_) {
            // Plan D4 rule 3: drop the zero-filled idle carry so the onset is
            // sample-accurate (FR-015) and the next sample forces a
            // renderOneChunk -> updateControl. A LIVE retrigger's carry is real
            // program material and is KEPT: dropping it would skip up to 63
            // rendered samples and create exactly the click SC-004 measures.
            carryAvail_ = 0;
            carryRead_ = 0;
        }
        velocity_ = std::clamp(isFiniteBits(velocity) ? velocity : 0.0f, 0.0f, 1.0f);
        body_.setNoteFrequencyHz(frequencyHz);  // continuous_body.h:982
        cloud_.setFundamentalHz(frequencyHz);   // harmonic_cloud.h:383
        cloud_.noteOn();                        // :635 - redraws phases only when quiescent
        mse_.gate(true);                        // multi_stage_envelope.h:99
        if (envMode_ == EnvelopeMode::Growth) {
            growth_.trigger();  // growth_envelope.h:161 - no-op while Rising
        }
    }

    /// FR-024. Release the excitation only. body_, atmos_ and orbit_ keep
    /// running - they ARE the tail (RA-2). growth_ is deliberately not reset.
    void noteOff() noexcept {
        cloud_.noteOff();  // harmonic_cloud.h:663
        mse_.gate(false);
    }

    // =========================================================================
    // Seeding (FR-016)
    // =========================================================================

    /// Applies immediately when the voice is prepared. ContinuousBody::setSeed is
    /// configure-time only, so callers seed before the first note.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        if (prepared_) {
            applySeeds();
        }
    }
    [[nodiscard]] std::uint32_t getSeed() const noexcept { return seed_; }

    // =========================================================================
    // Envelope
    // =========================================================================

    /// FR-021. Growth mode forces EVERY stage from 0 up to and including
    /// sustainPoint-1 to 0 ms, preserving level and curve. Zeroing stage 0 alone
    /// is not enough: advanceToNextStage() only enters Sustaining when
    /// currentStage_ == sustainPoint_ (multi_stage_envelope.h:386-389), so
    /// FR-020's 4 s stage-1 ramp would still shape the composite.
    void setEnvelopeMode(EnvelopeMode mode) noexcept {
        if (mode == envMode_) {
            return;
        }
        envMode_ = mode;
        const int sustain = mse_.getSustainPoint();
        for (int st = 0; st < sustain; ++st) {
            const auto i = static_cast<std::size_t>(st);
            const float ms = (mode == EnvelopeMode::Growth) ? 0.0f : stageTimeMs_[i];
            mse_.setStage(st, stageLevel_[i], ms, kStageCurve);
        }
    }

    void setGrowthDurationSeconds(float seconds) noexcept {  // FR-022
        growth_.setDuration(seconds);
    }

    /// FR-030. One write path only: the shadow always takes the caller's value
    /// (so the getter reads back what was set, even in Growth mode where the
    /// pre-sustain time is stored but not applied).
    void setEnvelopeStageTimeMs(int stage, float ms) noexcept {
        if (stage < 0 || stage >= MultiStageEnvelope::kMaxStages) {
            return;
        }
        applyStage(stage, stageLevel_[static_cast<std::size_t>(stage)], ms);
    }

    void setEnvelopeReleaseMs(float ms) noexcept {  // FR-030
        releaseMs_ = ms;
        mse_.setReleaseTime(ms);
    }

    [[nodiscard]] float getEnvelopeStageTimeMs(int stage) const noexcept {
        if (stage < 0 || stage >= MultiStageEnvelope::kMaxStages) {
            return 0.0f;
        }
        return stageTimeMs_[static_cast<std::size_t>(stage)];
    }
    [[nodiscard]] float getEnvelopeReleaseMs() const noexcept { return releaseMs_; }
    [[nodiscard]] EnvelopeMode getEnvelopeMode() const noexcept { return envMode_; }
    /// FR-085. Composite envelope gain actually applied to the chain.
    [[nodiscard]] float getEnvelopeOutput() const noexcept { return envOutput_; }

    // =========================================================================
    // Spatial (FR-025, FR-026)
    // =========================================================================

    void setSpatialDepth(float normalized) noexcept { orbit_.setDepth(normalized); }  // :185
    void setSpatialRate(float hz) noexcept { orbit_.setRate(hz); }                    // :167
    void setSpatialCoupling(float normalized) noexcept { orbit_.setCoupling(normalized); }
    void setSpatialGrowth(float growth) noexcept { orbit_.setGrowth(growth); }        // :179

    /// FR-062's `VoiceWidth` macro target. Sets the CENTRE the orbit's y
    /// modulates around; the per-chunk width is
    /// `clamp(widthBase_ + y * kVoiceWidthSpanPct, kMinVoiceWidthPct, kMaxVoiceWidthPct)`
    /// (plan §2.6). Without a settable centre the macro row would be inert:
    /// the spatial stage recomputes the width every control chunk, so anything
    /// written straight into the M/S stage is overwritten within <= 64 samples.
    void setVoiceWidthBasePercent(float pct) noexcept {
        if (!isFiniteBits(pct)) {
            return;
        }
        widthBase_ = std::clamp(pct, kMinVoiceWidthPct, kMaxVoiceWidthPct);
    }
    [[nodiscard]] float getVoiceWidthBasePercent() const noexcept { return widthBase_; }
    /// Orbit x, [-1, +1].
    [[nodiscard]] float getSpatialAzimuth() const noexcept { return orbit_.getCurrentValue(); }
    /// [kMinVoiceWidthPct, kMaxVoiceWidthPct].
    [[nodiscard]] float getSpatialWidthPercent() const noexcept { return widthPct_; }

    // =========================================================================
    // Engine parameter surface (FR-030) - one-to-one forwarders, no added clamping
    // =========================================================================

    // -- HarmonicCloud (Phase 2) ---------------------------------------------
    void setRichness(float r) noexcept { cloud_.setRichness(r); }
    void setInharmonicity(float B) noexcept { cloud_.setInharmonicity(B); }
    void setSpectralTiltDb(float dbPerOct) noexcept { cloud_.setSpectralTiltDb(dbPerOct); }
    void setMutation(float m) noexcept { cloud_.setMutation(m); }
    void setSpectralGravity(float g) noexcept { cloud_.setSpectralGravity(g); }
    void setDriftDepthCents(float cents) noexcept { cloud_.setDriftDepthCents(cents); }
    void setStereoSpread(float spread) noexcept { cloud_.setStereoSpread(spread); }
    void setAttackTimeSec(float seconds) noexcept { cloud_.setAttackTimeSec(seconds); }
    void setDecayTimeSec(float seconds) noexcept { cloud_.setDecayTimeSec(seconds); }

    /// @brief Phase 11 FR-030/FR-031. The three per-partial authoring surfaces,
    ///        on the same "no added clamping" contract as the block banner.
    ///
    /// MASK POLARITY: HarmonicCloud::setPartialMask's body is
    /// `masked_[index] = !active` (harmonic_cloud.h:1084-1089), so
    /// `active == true` means AUDIBLE and `active == false` means SILENCED;
    /// clearPartialMask() is `masked_.fill(false)` (:1101), i.e. everything
    /// audible. The plugin-side CloudFrame::maskBits convention is the OPPOSITE
    /// sense (bit set <=> masked), so every plugin call must invert.
    ///
    /// NO SECOND GUARD: the owner already rejects an out-of-range index and a
    /// non-finite position (harmonic_cloud.h:1070-1075, :1085-1087). Adding one
    /// here would only let the two surfaces disagree about what was stored.
    ///
    /// @par Thread ownership: AUDIO THREAD ONLY (or the host thread with the
    ///      audio thread stopped). These write `panPosition_`,
    ///      `positionOverridden_`, `panLeft_`/`panRight_` (harmonic_cloud.h:1069-1079,
    ///      updatePanGains at :1818-1834) and `masked_` (:1084-1089) - all of
    ///      which HarmonicCloud::process() reads and writes. Calling them from
    ///      the message thread is a data race.
    void setPartialPosition(std::size_t i, float p) noexcept { cloud_.setPartialPosition(i, p); }
    void setPartialMask(std::size_t i, bool active) noexcept { cloud_.setPartialMask(i, active); }
    void clearPartialMask() noexcept { cloud_.clearPartialMask(); }

    // -- SpectralMorphEngine (Phase 3) ---------------------------------------
    void setEntropy(float e) noexcept { morph_.setEntropy(e); }
    void setBloom(float bloom) noexcept { morph_.setBloom(bloom); }
    /// NOT configure-time gated: FR-062's Bloom row needs it live.
    void setTargetPosition(float p) noexcept { morph_.setTargetPosition(p); }
    void setTravelRate(float journeysPerSecond) noexcept {
        morph_.setTravelRate(journeysPerSecond);
    }

    /// Called out separately because its parameter is a CLASS-SCOPED nested enum
    /// (spectral_morph_engine.h:139). Unqualified `TravelMode` names nothing from
    /// SeraphisVoice's scope, so it is spelled out in full at every use site.
    void setTravelMode(SpectralMorphEngine::TravelMode mode) noexcept {
        morph_.setTravelMode(mode);
    }
    [[nodiscard]] SpectralMorphEngine::TravelMode getTravelMode() const noexcept {
        return morph_.getTravelMode();
    }

    // -- ContinuousBody (Phase 4) --------------------------------------------
    /// `BodyMaterial` is likewise class-scoped (continuous_body.h:81).
    void setMaterial(ContinuousBody::BodyMaterial m) noexcept { body_.setMaterial(m); }
    void setResonance(float v) noexcept { body_.setResonance(v); }
    void setDamping(float v) noexcept { body_.setDamping(v); }
    void setKeyTracking(float v) noexcept { body_.setKeyTracking(v); }
    void setDrive(float v) noexcept { body_.setDrive(v); }
    void setMix(float v) noexcept { body_.setMix(v); }
    void setCloudMix(float v) noexcept { body_.setCloudMix(v); }
    void setCloudDecaySec(float v) noexcept { body_.setCloudDecaySec(v); }
    void setCloudSize(float v) noexcept { body_.setCloudSize(v); }
    void setCloudDamping(float v) noexcept { body_.setCloudDamping(v); }
    void setWidth(float v) noexcept { body_.setWidth(v); }

    // -- AtmosphereEngine (Phase 5) ------------------------------------------
    void setLevel(float level) noexcept { atmos_.setLevel(level); }
    void setBlur(float amount) noexcept { atmos_.setBlur(amount); }
    void setDensity(float grainsPerSecond) noexcept { atmos_.setDensity(grainsPerSecond); }
    void setGrainSeconds(float seconds) noexcept { atmos_.setGrainSeconds(seconds); }
    void setDriftDepth(float depth) noexcept { atmos_.setDriftDepth(depth); }
    void setPanSpread(float spread) noexcept { atmos_.setPanSpread(spread); }
    void setDecorrelation(float amount) noexcept { atmos_.setDecorrelation(amount); }
    void setFreezeMix(float mix) noexcept { atmos_.setFreezeMix(mix); }

    // =========================================================================
    // Phase 9 parameter surface (FR-070) - thirteen FURTHER one-to-one
    // forwarders, on the same "no added clamping" contract as the block above.
    //
    // Every owner below already substitutes a non-finite argument with its own
    // documented default and clamps into its own range, so a second guard here
    // would only make the two surfaces disagree about what was stored.
    //
    // @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
    // @par Real-Time Safety: allocation-free, lock-free, exception-free - each
    //      is a single noexcept scalar store on an owned sub-component.
    // =========================================================================

    // -- HarmonicCloud, Phase 9 additions (FR-070 #1, #2) --------------------
    /// PREFIXED: the bare `setDriftSmoothness` would be ambiguous between
    /// HarmonicCloud (harmonic_cloud.h:513) and AtmosphereEngine
    /// (atmosphere_engine.h:844), both of which this facade reaches. Phase 7's
    /// existing bare setDriftDepthCents / setDriftDepth are NOT renamed.
    void setCloudDriftSmoothness(float s) noexcept { cloud_.setDriftSmoothness(s); }
    void setEnvelopeOffsetSpread(float spread) noexcept {
        cloud_.setEnvelopeOffsetSpread(spread);      // harmonic_cloud.h:580
    }

    // -- AtmosphereEngine drift (FR-070 #3, #4) ------------------------------
    void setAtmosDriftSmoothness(float s) noexcept { atmos_.setDriftSmoothness(s); }   // :844
    void setAtmosDriftRangeSemitones(float st) noexcept {
        atmos_.setDriftRangeSemitones(st);           // :852
    }

    // -- SpectralMorphEngine spline shape (FR-070 #5) ------------------------
    /// `double`, matching the owner's signature (spectral_morph_engine.h:385),
    /// which rejects a non-finite argument itself (:386-388).
    void setWaypointInterval(double seconds) noexcept { morph_.setWaypointInterval(seconds); }

    // -- AtmosphereEngine placement / transposition (FR-070 #6-#11) ----------
    /// All six already ship matching getters (:803, :811, :819, :826, :833,
    /// :962), so FR-072 creates nothing for them.
    void setAtmosJitter(float amount) noexcept { atmos_.setJitter(amount); }              // :800
    void setAtmosPositionSeconds(float s) noexcept { atmos_.setPositionSeconds(s); }      // :807
    void setAtmosPositionSpread(float sp) noexcept { atmos_.setPositionSpread(sp); }      // :815
    void setAtmosPitchSemitones(float st) noexcept { atmos_.setPitchSemitones(st); }      // :822
    void setAtmosPitchSpread(float sp) noexcept { atmos_.setPitchSpread(sp); }            // :830
    /// A plain store over windows prepare() already generated (:954-961), which
    /// is what makes it drivable from an automation lane at block rate.
    void setAtmosGrainEnvelope(GrainEnvelopeType t) noexcept {
        atmos_.setGrainEnvelope(t);                  // :959
    }

    // -- ContinuousBody character switches (FR-070 #12, #13) -----------------
    /// FR-070 #12. Turning the AGC OFF makes the body a FIXED gain (the
    /// excitation-comp seed, the estimator and rmsGain_ all switch off with it,
    /// continuous_body.h:2901-2919, :3029-3046, :3683-3689). The resulting level
    /// change is documented behaviour, not a defect.
    void setBodyInputAgcEnabled(bool enabled) noexcept {
        body_.setInputAgcEnabled(enabled);           // continuous_body.h:1276
    }
    /// FR-070 #13. body_.setResonatorBypass is SELF-GUARDING (:1302-1304) and
    /// applies its own 10 ms equal-power ramp plus the mandatory waveguide
    /// re-tune on un-bypass (:1311-1320). This forwarder MUST NOT add a second
    /// guard.
    void setBodyResonatorBypass(bool bypass) noexcept {
        body_.setResonatorBypass(bypass);            // :1300
    }

    // =========================================================================
    // Spectral state forwarders (FR-031, RELAXED by Phase 11 FR-033a / D-1)
    // =========================================================================

    /// Phase 11 FR-033a (D1). NOT configure-time gated: SpectralMorphEngine::setState
    /// absorbs a live state swap through the FR-047 fade (spectral_morph_engine.h:312,
    /// slotContributes() at :558), which Phase 3's FR-042/FR-044 already prove
    /// continuity-safe. The Phase 9 gate was SeraphisVoice's own extra restriction and
    /// made a Phase 11 partial edit inaudible until the next note-on.
    void setSpectralState(int slot, const SpectralState& s) noexcept { morph_.setState(slot, s); }
    void setSpectralStateCount(int n) noexcept { morph_.setStateCount(n); }
    /// KEPT (plan R-17), NOT dead: SC-028 arm (ii) asserts this counter is
    /// UNCHANGED across a live spectral push, so it is the observable that proves
    /// the relaxation landed. Deleting it deletes the criterion.
    [[nodiscard]] std::uint32_t getRejectedConfigureTimeCallCount() const noexcept {
        return rejectedConfigCalls_;
    }

    // =========================================================================
    // Freeze (FR-030a)
    // =========================================================================

    void captureFreeze() noexcept { atmos_.captureFreeze(); }   // atmosphere_engine.h:909
    void releaseFreeze() noexcept { atmos_.releaseFreeze(); }   // :928
    [[nodiscard]] bool isFreezeCaptured() const noexcept { return atmos_.isFreezeCaptured(); }

    // =========================================================================
    // Introspection (FR-085)
    // =========================================================================

    /// FR-033. Absolute peak per control chunk into a one-pole with instant
    /// attack and a kLevelReleaseMs time constant, sampled at chunk boundaries.
    [[nodiscard]] float getCurrentLevel() const noexcept { return level_; }

    /// FR-032. Envelope idle AND cloud quiescent AND the audio has been below
    /// kTailSilenceThreshold for kQuiescentChunksToRetire consecutive chunks.
    [[nodiscard]] bool isFinished() const noexcept {
        return !mse_.isActive()                                // multi_stage_envelope.h:251
               && cloud_.isQuiescent()                         // harmonic_cloud.h:1040
               && quiescentChunks_ >= kQuiescentChunksToRetire;
    }

    /// True once at least one control chunk has been rendered since the last
    /// noteOn(). Plan D4 rule 2 - the engine gates its bloom snapshot on this,
    /// because it is the only observable proof that cloud_.updateControl has
    /// consumed freqDirty_ and recomputed frequencyHz_[].
    [[nodiscard]] bool hasRenderedSinceNoteOn() const noexcept { return renderedSinceNoteOn_; }

    /// FR-035. Bit-pattern finiteness sweep over the voice's own state.
    [[nodiscard]] bool stateFinite() const noexcept {
        return body_.stateFinite()      // continuous_body.h:1328
               && morph_.stateFinite()  // spectral_morph_engine.h:456
               && isFiniteBits(level_) && isFiniteBits(lastOutL_) && isFiniteBits(lastOutR_)
               && isFiniteBits(gainLSm_.getCurrentValue())
               && isFiniteBits(gainRSm_.getCurrentValue());
    }

    [[nodiscard]] const HarmonicCloud& cloud() const noexcept { return cloud_; }
    [[nodiscard]] const SpectralMorphEngine& morph() const noexcept { return morph_; }
    [[nodiscard]] const ContinuousBody& body() const noexcept { return body_; }
    [[nodiscard]] const AtmosphereEngine& atmos() const noexcept { return atmos_; }
    /// FIFTH sub-component accessor, beyond FR-085's enumerated list (plan §10 V-8):
    /// SC-010 clause 1 reads back OrbitModulator::getDepth/getRate/getCoupling/
    /// getGrowth (orbit_modulator.h:189-192) through SeraphisVoice's forwarders,
    /// and the four FR-019 spatial rows have no other reachable read-back.
    [[nodiscard]] const OrbitModulator& orbit() const noexcept { return orbit_; }

    // =========================================================================
    // Phase 9 read-back accessor (FR-072)
    //
    // @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
    // @par Real-Time Safety: a pure const member read - allocation-free,
    //      lock-free, exception-free.
    // =========================================================================

    /// SIXTH sub-component accessor. GrowthEnvelope::getDuration()
    /// (growth_envelope.h:149) is the only read-back for the growth-duration
    /// parameter and is unreachable without this.
    [[nodiscard]] const GrowthEnvelope& growth() const noexcept { return growth_; }

private:
    /// SC-003 positive control (b) - see the declaration above this class.
    friend struct detail::SeraphisVoiceSilenceRampProbe;

    // =========================================================================
    // Helpers
    // =========================================================================

    /// Plan §2.12. NEVER std::isnan / std::isinf / std::numeric_limits: the macOS
    /// leg builds with -ffast-math, which licenses the compiler to fold them away.
    /// Copied verbatim from continuous_body.h:1346-1351. A plain, inlinable static
    /// - NOT the ITERUM_NOINLINE wrapper, whose own header forbids per-sample use
    /// (atmosphere_engine.h:1203-1206).
    [[nodiscard]] static bool isFiniteBits(float v) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        return (bits & 0x7F800000u) != 0x7F800000u;
    }

    /// Plan §2.4. GrowthEnvelope::setSeed is a documented no-op
    /// (growth_envelope.h:140) and is deliberately NOT called.
    /// deriveStreamSeed substitutes 0x2545F491u when the hash lands on 0
    /// (core/random.h:110), which is why seed 0 is legal.
    void applySeeds() noexcept {
        cloud_.setSeed(deriveStreamSeed(seed_, kCloudSalt));  // core/random.h:102
        morph_.setSeed(deriveStreamSeed(seed_, kMorphSalt));
        body_.setSeed(deriveStreamSeed(seed_, kBodySalt));
        atmos_.setSeed(deriveStreamSeed(seed_, kAtmosSalt));
        orbit_.setSeed(deriveStreamSeed(seed_, kOrbitSalt));
    }

    /// Plan §2.3 step 7. THE single write path into mse_'s stage configuration.
    ///
    /// The shadow arrays are the single source of truth: setEnvelopeMode's
    /// Standard branch restores from them, so a direct mse_.setStage anywhere
    /// would leave them stale and a Standard -> Growth -> Standard round trip
    /// would silently install a 0 ms attack.
    void applyStage(int st, float level, float ms) noexcept {
        if (st < 0 || st >= MultiStageEnvelope::kMaxStages) {
            return;
        }
        const auto i = static_cast<std::size_t>(st);
        // FR-059 idempotence guard. MultiStageEnvelope::setStage has NO equality
        // early-out of its own, so without this an unchanged-knob apply() every
        // block would keep rewriting the stage.
        if (stageLevel_[i] == level && stageTimeMs_[i] == ms) {
            return;
        }
        stageLevel_[i] = level;
        stageTimeMs_[i] = ms;
        if (envMode_ == EnvelopeMode::Standard || st >= mse_.getSustainPoint()) {
            mse_.setStage(st, level, ms, kStageCurve);  // multi_stage_envelope.h:166
        } else {
            mse_.setStage(st, level, 0.0f, kStageCurve);  // Growth zeroes pre-sustain times
        }
    }

    /// Plan §2.11's predicate. "Prepared but not currently sounding."
    [[nodiscard]] bool isConfigurable() const noexcept { return !hasSounded_ || isFinished(); }

    /// Plan §2.3. The shared body of reset()/resetForSteal(); the ONLY difference
    /// between the two entry points is the D3 fade tail, handled by the callers.
    void resetRunState() noexcept {
        cloud_.reset();
        morph_.reset();
        body_.reset();
        atmos_.reset();
        mse_.reset();
        growth_.reset();
        orbit_.reset();
        // The width is PUSHED IN before ms_.reset(), not left to it.
        // MidSideProcessor::reset() snaps its width smoother to `width_` - the
        // last value applySpatialStage handed to setWidth (midside_processor.h:
        // 113-119 against :133-136) - so a bare reset() would leave a recycled
        // voice starting from the stale width of the note it just abandoned and
        // ramping away from it, while a freshly prepared voice starts at 100 %.
        // That is exactly the FR-005 "post-prepare render" claim.
        //
        // DEFENSIVE, AND SAID SO: at the FR-019 neutral this line changes the
        // output by at most 1.6e-7 per sample and NO TEST IN THE SUITE CAN
        // DISCRIMINATE IT. The width scales the SIDE signal only, and the bus
        // reaching this stage is very nearly mono - ContinuousBody sends its
        // (mono) resonator output to both channels (continuous_body.h FR-062),
        // the whole voice sits at -52 dBFS, and the decorrelated contributors
        // (the body decay cloud, the atmosphere) have barely developed inside
        // any window short enough to stay clear of the body path dependence
        // documented in SeraphisVoice_PrepareAndResetAreIdempotent. MEASURED,
        // with the line removed and the width driven to its 150 % clamp for the
        // warm render: max |reset - virgin| = 4.5e-8 L / 9.7e-8 R at the FR-019
        // cloud mix, 1.0e-7 / 1.6e-7 with setCloudMix(1.0). It is kept because
        // it is a state restoration that costs one line and becomes audible the
        // moment the bus carries real width.
        ms_.setWidth(100.0f);  // the same unity the widthPct_ reset below states
        ms_.reset();

        cloudL_.fill(0.0f);
        cloudR_.fill(0.0f);
        bodyL_.fill(0.0f);
        bodyR_.fill(0.0f);
        atmosL_.fill(0.0f);
        atmosR_.fill(0.0f);
        carryL_.fill(0.0f);
        carryR_.fill(0.0f);
        carryAvail_ = 0;
        carryRead_ = 0;
        carryIsLifeOnly_ = true;

        // The centre-position balance gain, so a reset voice does not fade in
        // over kSpatialSmoothMs and two identically seeded voices (one freshly
        // prepared, one reset) start from identical smoother state.
        gainLSm_.snapTo(1.0f);
        gainRSm_.snapTo(1.0f);
        widthPct_ = 100.0f;

        level_ = 0.0f;
        // SEEDED AT THE RETIRE VALUE, not 0 (plan V-15). The counter is only ever
        // advanced inside renderOneChunk/advanceOneChunkLifeOnly, so with a 0 seed
        // every never-rendered slot would be isFinished() == false and the engine
        // would take the full render path on all 16 slots for the first 4 chunks.
        quiescentChunks_ = kQuiescentChunksToRetire;
        hasSounded_ = false;
        renderedSinceNoteOn_ = false;
        lastOutL_ = 0.0f;
        lastOutR_ = 0.0f;
        envOutput_ = 0.0f;
        velocity_ = 1.0f;
    }

    /// Plan §2.5. Exactly kControlChunkSamples samples, always.
    void renderOneChunk() noexcept {
        constexpr std::size_t n = kControlChunkSamples;

        // 0. Plan D4 rule 2, before any work: step 2 is what consumes freqDirty_.
        renderedSinceNoteOn_ = true;

        // 1. morph -> cloud handoff, unconditionally every chunk (FR-012). The
        //    whole-array skip at harmonic_cloud.h:776-786 makes an unchanged
        //    target cheap, so the voice does not duplicate that check.
        morph_.updateChunk(n);  // spectral_morph_engine.h:405
        cloud_.setSpectralTarget(morph_.getOutputRatios(), morph_.getOutputAmplitudes(),
                                 morph_.getOutputCount());  // harmonic_cloud.h:769

        // 2. excitation
        cloud_.processStereoBlock(cloudL_.data(), cloudR_.data(), n);  // :878

        // 3. voice envelope, IN PLACE on the cloud - the excitation gate.
        //    Nothing downstream of this point is gated (FR-010).
        if (envMode_ == EnvelopeMode::Growth) {
            growth_.processBlock(n);                       // growth_envelope.h:185
            const float gGrowth = growth_.getCurrentValue();  // :197, held across the chunk
            for (std::size_t s = 0; s < n; ++s) {
                const float g = velocity_ * gGrowth * mse_.process();
                cloudL_[s] *= g;
                cloudR_[s] *= g;
                envOutput_ = g;
            }
        } else {
            for (std::size_t s = 0; s < n; ++s) {
                const float g = velocity_ * mse_.process();  // multi_stage_envelope.h:223
                cloudL_[s] *= g;
                cloudR_[s] *= g;
                envOutput_ = g;
            }
        }

        // 4. body. NOT in place - "IN-PLACE OPERATION IS NOT SUPPORTED"
        //    (continuous_body.h:1155-1156).
        body_.processStereoBlock(cloudL_.data(), cloudR_.data(), bodyL_.data(), bodyR_.data(), n);

        // 5. atmosphere tap, reading the POST-BODY signal. Shape identical by
        //    design (atmosphere_engine.h:656-658); output is WET TEXTURE ONLY.
        atmos_.processStereoBlock(bodyL_.data(), bodyR_.data(), atmosL_.data(), atmosR_.data(), n);

        // 6. voice bus = body + atmosphere. A PLAIN SUM, no second gain: the
        //    atmosphere's own trim is already applied inside the component
        //    (setLevel is "Output gain trim", atmosphere_engine.h:944-949,
        //    multiplied at :2233), and multiplying by getLevel() again would
        //    square it. cloudL_/cloudR_ are free by now and serve as the bus
        //    scratch, which is why §2.2 declares exactly eight buffers.
        for (std::size_t s = 0; s < n; ++s) {
            cloudL_[s] = bodyL_[s] + atmosL_[s];
            cloudR_[s] = bodyR_[s] + atmosR_[s];
        }

        // 7. spatial stage (FR-025, RA-3) -> carryL_/carryR_
        applySpatialStage(n);

        // 8. silence fade tail (plan D3). GUARDED: the ramp is 48 samples at
        //    48 kHz inside a 64-sample chunk, and an unguarded post-decrement
        //    would run negative and add an inverted, magnitude-GROWING tail on
        //    every subsequent chunk forever.
        for (std::size_t s = 0; s < n && fadeRemaining_ > 0; ++s) {
            const float w =
                static_cast<float>(fadeRemaining_) / static_cast<float>(silenceRampSamples_);
            carryL_[s] += fadeTailL_ * w;
            carryR_[s] += fadeTailR_ * w;
            --fadeRemaining_;
        }

        // 9. level detector + retirement counter (FR-032, FR-033), measured on
        //    the POST-spatial buffer, i.e. exactly what the voice contributes.
        float chunkPeak = 0.0f;
        for (std::size_t s = 0; s < n; ++s) {
            chunkPeak = std::max(chunkPeak, std::max(std::fabs(carryL_[s]), std::fabs(carryR_[s])));
        }
        updateLevel(chunkPeak);

        // 10. lastOut* is NOT assigned here - it is captured at SERVE time.
        carryAvail_ = n;
        carryRead_ = 0;
        carryIsLifeOnly_ = false;
    }

    /// Plan §2.8. One life-only chunk: the orbit ticks, the level detector
    /// releases at chunk peak 0, and the carry is refilled with silence.
    ///
    /// The spatial stage runs on a ZEROED bus rather than being skipped. §2.8's
    /// invariant is that advancing `n` samples through advanceLifeOnly and
    /// through processStereoBlock leaves getSpatialAzimuth() AND
    /// getSpatialWidthPercent() identical for EVERY n - and widthPct_ is
    /// computed inside applySpatialStage, so ticking orbit_ alone would leave a
    /// life-only voice reporting the stale 100 % while a rendering one reports
    /// `100 + y*50`. Running the stage on silence also leaves the two balance
    /// smoothers and the M/S stage in exactly the state the rendering path would
    /// have reached, so an idle -> sounding transition does not step. It costs
    /// 64 multiplies plus one M/S pass - nothing against the full chain this
    /// path exists to skip - and a zero bus through an equal-power gain and the
    /// M/S matrix is exactly 0.0f, so the carry it fills is still bit-zero.
    void advanceOneChunkLifeOnly() noexcept {
        constexpr std::size_t n = kControlChunkSamples;
        cloudL_.fill(0.0f);
        cloudR_.fill(0.0f);
        applySpatialStage(n);  // ticks orbit_ (:216), sets widthPct_, fills carry* with 0
        updateLevel(0.0f);
        carryAvail_ = n;
        carryRead_ = 0;
        carryIsLifeOnly_ = true;
        // Plan D3: a non-rendering voice emits silence, so its "last emitted
        // sample" is 0 and an armed tail must not start from stale program audio.
        lastOutL_ = 0.0f;
        lastOutR_ = 0.0f;
    }

    /// Plan §2.7. Instant attack, kLevelReleaseMs release, once per chunk.
    void updateLevel(float chunkPeak) noexcept {
        level_ = (chunkPeak > level_)
                     ? chunkPeak
                     : chunkPeak + (level_ - chunkPeak) * levelReleaseCoeff_;
        quiescentChunks_ = (level_ < kTailSilenceThreshold) ? (quiescentChunks_ + 1) : 0;
    }

    /// Plan §2.6. Equal-power azimuth BALANCE normalised to unity at centre,
    /// then M/S width. Reads the bus from cloudL_/cloudR_ and writes carry*.
    ///
    /// equalPowerGains (core/crossfade_utils.h:50-53) is a MONO panner - its raw
    /// cos/sin pair applied as a balance attenuates a centred stereo bus by 3 dB
    /// - so the pair is scaled by sqrt(2): at panNorm = 0.5, cos = sin = 0.70710678
    /// and the product is exactly 1.0 on both channels, while gL^2 + gR^2 = 2 at
    /// every position, so the law stays constant-power (FR-026).
    ///
    /// getGrowth() is NEVER read: its documented neutral is 0
    /// (orbit_modulator.h:177-180) and growth is already baked into the radius
    /// that getY() returns.
    void applySpatialStage(std::size_t n) noexcept {
        orbit_.processBlock(n);
        const float x = orbit_.getCurrentValue();  // orbit_modulator.h:236, clamped +-1
        const float y = orbit_.getY();             // :242

        const float panNorm = (x + 1.0f) * 0.5f;
        float gL = 0.0f;
        float gR = 0.0f;
        equalPowerGains(panNorm, gL, gR);
        gL *= kSqrt2;
        gR *= kSqrt2;
        gainLSm_.setTarget(gL);
        gainRSm_.setTarget(gR);

        // The orbit modulates around a SETTABLE centre, not around a literal 100.
        widthPct_ = std::clamp(widthBase_ + y * kVoiceWidthSpanPct, kMinVoiceWidthPct,
                               kMaxVoiceWidthPct);
        ms_.setWidth(widthPct_);  // midside_processor.h:133

        for (std::size_t s = 0; s < n; ++s) {
            cloudL_[s] *= gainLSm_.process();  // smoother.h:197
            cloudR_[s] *= gainRSm_.process();
        }
        // MidSideProcessor is verified in-place-safe (it reads L/R into locals
        // first, midside_processor.h:195-196), but distinct buffers are used here.
        ms_.process(cloudL_.data(), cloudR_.data(), carryL_.data(), carryR_.data(), n);
    }

    // =========================================================================
    // State (plan §2.2). No std::vector, no std::function, no smart pointer, no
    // std::string anywhere - SC-008's grep is a flat zero.
    // =========================================================================

    // --- owned sub-components (FR-002; exactly one each, by value) -----------
    HarmonicCloud cloud_;
    SpectralMorphEngine morph_;
    ContinuousBody body_;
    AtmosphereEngine atmos_;
    MultiStageEnvelope mse_;
    GrowthEnvelope growth_;
    OrbitModulator orbit_;
    MidSideProcessor ms_;

    // --- fixed-size scratch, ONE control chunk each (plan D1, V-1: 2 KB/voice)
    std::array<float, kControlChunkSamples> cloudL_{};
    std::array<float, kControlChunkSamples> cloudR_{};
    std::array<float, kControlChunkSamples> bodyL_{};
    std::array<float, kControlChunkSamples> bodyR_{};
    std::array<float, kControlChunkSamples> atmosL_{};
    std::array<float, kControlChunkSamples> atmosR_{};
    std::array<float, kControlChunkSamples> carryL_{};
    std::array<float, kControlChunkSamples> carryR_{};
    std::size_t carryAvail_ = 0;
    std::size_t carryRead_ = 0;
    /// Plan D4 rule 3: the carry currently held was produced by
    /// advanceOneChunkLifeOnly (zeros), not renderOneChunk.
    bool carryIsLifeOnly_ = true;

    // --- spatial --------------------------------------------------------------
    OnePoleSmoother gainLSm_;
    OnePoleSmoother gainRSm_;
    float widthPct_ = 100.0f;   ///< last value pushed into ms_.setWidth
    float widthBase_ = 100.0f;  ///< FR-062 VoiceWidth macro target; §2.6's centre

    // --- level detector (FR-033) ----------------------------------------------
    float level_ = 0.0f;
    float levelReleaseCoeff_ = 0.0f;  ///< plan D2
    int quiescentChunks_ = kQuiescentChunksToRetire;

    // --- envelope --------------------------------------------------------------
    EnvelopeMode envMode_ = EnvelopeMode::Standard;
    float velocity_ = 1.0f;
    float envOutput_ = 0.0f;
    /// FR-030 shadows - the SINGLE SOURCE OF TRUTH for the stage configuration.
    std::array<float, static_cast<std::size_t>(MultiStageEnvelope::kMaxStages)> stageTimeMs_{};
    std::array<float, static_cast<std::size_t>(MultiStageEnvelope::kMaxStages)> stageLevel_{};
    float releaseMs_ = 8000.0f;

    // --- silence carry (plan D3) ------------------------------------------------
    float fadeTailL_ = 0.0f;
    float fadeTailR_ = 0.0f;
    float lastOutL_ = 0.0f;
    float lastOutR_ = 0.0f;
    int fadeRemaining_ = 0;
    int silenceRampSamples_ = 48;

    // --- bookkeeping -------------------------------------------------------------
    double sampleRate_ = 48000.0;
    bool prepared_ = false;
    /// §2.11 configure-time gate: set in noteOn(), cleared in reset()/resetForSteal().
    /// A never-noted voice is CONFIGURABLE.
    bool hasSounded_ = false;
    /// Plan D4 rule 2.
    bool renderedSinceNoteOn_ = false;
    std::uint32_t seed_ = 1u;
    std::uint32_t rejectedConfigCalls_ = 0u;
};

// FR-002's coarse ownership guard and FR-013's heap-free-giant guard.
static_assert(sizeof(SeraphisVoice) <= SeraphisVoice::kVoiceSizeBound,
              "FR-002: SeraphisVoice grew past its measured bound - check for a StereoField, "
              "VoiceModRouter, ModulationEngine, PolySynthEngine, SynthVoice or AetherReverb "
              "member, and re-record kVoiceSizeBound if the growth is legitimate");
static_assert(sizeof(SeraphisVoice) < 3 * 1024 * 1024, "FR-013");

}  // namespace DSP
}  // namespace Krate
