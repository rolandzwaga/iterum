// ==============================================================================
// Layer 3: System - SeraphisEngine (Seraphis polyphonic engine + output stage)
// ==============================================================================
// Seraphis Phase 7. Spec slug: seraphis-phase7-voice-engine.
//   Spec:    specs/seraphis-phase7-voice-engine/spec.md
//   Plan:    specs/seraphis-phase7-voice-engine/plan.md   (§3)
//   Roadmap: specs/Seraphis-roadmap.md, Part A -> Phase 7 (lines 288-315)
//
// kMaxVoices SeraphisVoice slots behind a VoiceAllocator, summed on the shared
// 64-sample control grid, then run through the output stage (TapeSaturator at
// low drive + TruePeakLimiter safety).
//
// THE REVERB IS NOT HERE. AetherReverb is Layer 4 and this is Layer 3, so the
//   voice-sum -> reverb -> output-stage chain is COMPOSED BY THE CALLER:
//   processStereoBlock() produces the voice sum only, and processOutputStage()
//   is a separate in-place call the caller makes AFTER its reverb. The FR-070
//   test helper (tests/test_helpers/seraphis_chain.h) is where the two halves
//   are joined for the composed-chain criteria; it may include Layer 4 because
//   tools/lint-layers.js scans only dsp/include/krate/dsp.
//
// LAYER DISCIPLINE (FR-056, FR-070). Layers 0-2 + Layer 3 peers only. NO
//   effects/ header, ever.
//
// REAL-TIME SAFETY CONTRACT (FR-041). prepare() is the ONLY method that
//   allocates; it prepares ALL kMaxVoices slots so setPolyphony() can never
//   allocate. reset() and silence() are noexcept and allocation-free but are
//   NOT audio-thread operations - see their declarations (plan §9 R13).
//
// BUILD STATE. T005 (tasks.md) lands plan §3.1-§3.5: the pool, prepare, the
//   absolute-grid render loop, the FR-052 sum gain, the FR-044 deferred
//   retirement and the FR-053/053a/054 output stage. T006 lands plan §3.6,
//   §3.6.0 and §3.6.2: the provenance-split note dispatch, the FR-047 orphan
//   teardown and the polyphony-shrink handler that is the sole writer of
//   orphanTail_. T007 lands plan §3.6.1: the quietest-with-amnesty steal
//   selection (freeChosenVictimSlot()), which frees the victim slot BEFORE
//   allocator_.noteOn so the allocator's own search can only land the incoming
//   note on it. T008 lands the freeze fan-out (§3.8), the bloom collection
//   (§3.7, D4) and the FR-072 deferred recovery, together with the two
//   per-control-chunk service bounds (kFreezeRetriesPerChunk,
//   kResetsPerControlChunk) that keep an FFT storm and a 32 MiB memset storm out
//   of a single 1.33 ms chunk.
//
// FR-072 FAULT INJECTION IS NOT REACHABLE FROM THE PUBLIC API, and that is a
//   PROPERTY OF THE COMPOSED COMPONENTS, not an oversight here. Every
//   SeraphisVoice forwarder lands on a sub-component setter that sanitises its
//   argument (harmonic_cloud.h:384/413/427/440/453/479/502/536/557/569;
//   continuous_body.h:955/964/973/984/994/1003/1013/1024/1035/1048/1058;
//   atmosphere_engine.h:779/793/859/866/874/882/946 all use
//   `isFinite(x) ? x : default`), the two unsanitised ones (OrbitModulator's
//   std::clamp-only setters, orbit_modulator.h:167-186) funnel through
//   OnePoleSmoother::setTarget, which substitutes 0 for NaN
//   (smoother.h:170-181), ContinuousBody zero-substitutes both its mono path
//   (:1196-1200) and its final write (:2899-2902), and MidSideProcessor's width
//   goes through the same scrubbing smoother (midside_processor.h:133-136). No
//   legal call sequence can therefore make a voice emit a non-finite sample.
//   Phase 6 hit exactly this wall and resolved it with a fault-injection hook
//   (aether_reverb.h:2691-2722, `injectNonFiniteStateForTest`, whose comment
//   states the same "every input path is sealed" argument); the Layer 3
//   equivalent here is the friend probe declared just below, which is a
//   DECLARATION ONLY in the library and is defined solely by the test TU.
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/random.h>  // deriveStreamSeed (FR-050)

// Layer 1: Primitives
#include <krate/dsp/primitives/smoother.h>  // OnePoleSmoother (FR-052)

// Layer 2: Processors
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/true_peak_limiter.h>

// Layer 3: Systems (peers)
#include <krate/dsp/systems/seraphis_voice.h>
#include <krate/dsp/systems/voice_allocator.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
// FR-001's std::is_trivially_copyable_v guard. NOT optional and NOT
// transitively guaranteed: neither this file's other stdlib headers nor
// seraphis_voice.h's block (:48-74) includes it, and while MSVC and libstdc++
// commonly drag it in through <array>/<algorithm>, libc++ need not - so a
// toolchain that leaks it would go green while CI's did not.
#include <type_traits>

namespace Krate {
namespace DSP {

/// @brief Engine construction options; `voice` is forwarded verbatim to every slot.
struct SeraphisEngineConfig {
    SeraphisVoiceConfig voice{};
    /// FR-040 shipped default.
    std::size_t polyphony = 8;
    std::uint32_t seed = 1u;
};

/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
/// @par Real-Time Safety: plain data; copying is trivial and allocation-free.
///
/// FR-001. One field per VP-routed parameter of the Phase 9 surface (spec C-6).
/// NO FIELD HERE MAY NAME A SeraphisMacroTarget (spec C-1/FR-055): those 27
/// values reach the voices through SeraphisMacroMatrix::setTargetBase, and a
/// second write path would double-apply them.
///
/// It is *not* SeraphisVoiceConfig (seraphis_voice.h:105-120), which is
/// prepare-time and is not extended. Every default member initializer below is
/// the SHIPPED voice default, so broadcasting a default-constructed instance
/// into a freshly prepared pool is observably a no-op.
struct SeraphisVoiceParams {
    // -- HarmonicCloud (IDs 206, 209, 210) ---------------------------------
    float cloudDriftSmoothness   = 0.5f;    // harmonic_cloud.h:2131
    float cloudDecaySec          = 0.5f;    // seraphis_voice.h:298
    float cloudEnvOffsetSpread   = 0.0f;    // harmonic_cloud.h:2135

    // -- SpectralMorphEngine (IDs 401, 403, 404, 407) ----------------------
    float morphBloom             = 0.0f;    // seraphis_voice.h:302
    SpectralMorphEngine::TravelMode morphTravelMode =
        SpectralMorphEngine::TravelMode::External;              // :139
    float morphTravelRate        = SpectralMorphEngine::kMinTravelRate;  // :101, voice :303
    double morphWaypointSeconds  = 2.0;     // SplineTrajectory::kDefaultInterval

    // -- Spatial / life modulators (IDs 601, 602, 603) ---------------------
    float spatialRateHz          = 0.1f;    // seraphis_voice.h:331
    float spatialCoupling        = 0.0f;    // :332
    float spatialGrowth          = 0.0f;    // :333

    // -- Voice envelope (IDs 700, 701) -------------------------------------
    SeraphisVoice::EnvelopeMode envMode = SeraphisVoice::EnvelopeMode::Standard;  // :341
    float envGrowthDurationSec   = 10.0f;   // :364

    // -- ContinuousBody (IDs 800, 801, 803-812) ----------------------------
    ContinuousBody::BodyMaterial bodyMaterial =
        ContinuousBody::BodyMaterial::Glass;                    // :306
    float bodyResonance          = 0.7f;    // :307
    float bodyKeyTracking        = 1.0f;    // :309
    float bodyDrive              = 1.0f;    // :310
    float bodyMix                = 1.0f;    // :311
    float bodyCloudMix           = 0.25f;   // :313
    float bodyCloudDecaySec      = 4.0f;    // :314
    float bodyCloudSize          = 1.0f;    // :315
    float bodyCloudDamping       = 0.3f;    // :316
    float bodyWidth              = 1.0f;    // :317
    bool  bodyInputAgc           = ContinuousBody::kDefaultAgcEnabled;       // :163 (true)
    bool  bodyResonatorBypass    = ContinuousBody::kDefaultResonatorBypass;  // :164 (false)

    // -- AtmosphereEngine (IDs 1002, 1003, 1005-1007, 1009-1016) -----------
    float atmosDensity           = 4.0f;    // seraphis_voice.h:322
    float atmosGrainSeconds      = 4.0f;    // :323
    float atmosPanSpread         = 0.7f;    // :325
    float atmosDecorrelation     = 0.5f;    // :326
    float atmosFreezeMix         = 0.0f;    // :327
    float atmosDriftSmoothness   = 0.7f;    // atmosphere_engine.h:843 (documented default)
    float atmosDriftRangeSemis   = 2.0f;    // :850
    float atmosJitter            = 0.5f;    // :798
    float atmosPositionSeconds   = 1.0f;    // :805-806
    float atmosPositionSpread    = 0.3f;    // :813-814
    float atmosPitchSemitones    = 0.0f;    // :820
    float atmosPitchSpread       = 0.15f;   // :828-829
    GrainEnvelopeType atmosGrainEnvelope = GrainEnvelopeType::Hann;  // :952

    /// FR-001. The spec's VP row count (C-6). A static_assert cannot count named
    /// fields, so the guard is this compile-time constant plus the behavioural
    /// sweep in seraphis_param_broadcast_test.cpp - NOT a sizeof assertion,
    /// which is padding-dependent and would break on a legal ABI difference.
    /// Any field added or removed without updating the C-6 table fails that test.
    static constexpr std::size_t kFieldCount = 37;
};

static_assert(std::is_trivially_copyable_v<SeraphisVoiceParams>,
              "FR-001: the broadcast POD is copied on the audio thread");

namespace detail {
/// @brief FR-072 fault-injection probe. DECLARED HERE, DEFINED ONLY BY A TEST TU.
///
/// The engine's per-sample non-finite guard (processStereoBlock, the
/// `nonFinitePending_ |= voiceBit(v)` branch) cannot be reached through the
/// public API - see the "FR-072 FAULT INJECTION" box in this file's banner for
/// the sanitiser-by-sanitiser evidence. This friend is how the DEFERRED,
/// BOUNDED half of FR-072 (at most kResetsPerControlChunk voice resets per
/// control chunk, one getNonFiniteRecoveryCount() increment per serviced slot)
/// is testable at all. It costs nothing at run time and adds no public surface:
/// the library never defines it, so a shipping build has no way to call it.
///
/// ODR: swept this session - `SeraphisEngineNonFiniteProbe` has zero matches in
/// dsp/ or plugins/.
struct SeraphisEngineNonFiniteProbe;
}  // namespace detail

/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
/// @par Real-Time Safety: everything except prepare(), reset() and silence() is
///      noexcept, allocation-free, lock-free.
/// @warning `voices_` is a by-value std::array<SeraphisVoice, kMaxVoices>, so a
///          SeraphisEngine is several hundred KB of storage. It must NEVER be a
///          test local - MSVC's default main-thread stack is 1 MiB. Heap-allocate
///          it (plan §6.3).
class SeraphisEngine {
public:
    // =========================================================================
    // Constants - ALL class-scoped (plan §3.1, §3.2)
    // =========================================================================

    /// FR-040. Roadmap line 290's upper bound.
    static constexpr std::size_t kMaxVoices = 16;
    /// FR-007. The shared control-rate grid, same value as SeraphisVoice's.
    static constexpr std::size_t kControlChunkSamples = 64;
    /// FR-004.
    static constexpr std::size_t kMaxBlockSamples = 2048;
    /// FR-050. Per-voice seed salt base; disjoint from every SeraphisVoice salt.
    static constexpr std::size_t kVoiceSaltBase = 0x9000;
    /// @brief FR-052. Voice-sum gain smoothing time.
    ///
    /// 100 ms, NOT the 20 ms of the master-gain family, and the difference is
    /// structural rather than a taste call: `masterGain_` is advanced ONCE PER
    /// OUTPUT SAMPLE by its owner, whereas this smoother is read once and HELD
    /// for a whole control chunk (runPreRenderControlStep, :1054-1055) so that
    /// the value is partition-invariant (FR-052, SC-014). A held value is a
    /// STAIRCASE, and its first stair is `1 - e^(-64/tau_samples)` of the whole
    /// step - 28.35 % at 20 ms.
    ///
    /// The largest legal step is polyphony 1 -> 2, i.e. `sumGainForPolyphony`
    /// moving 1.0 -> 0.7071 (:1005-1007), so at 20 ms one sample transition
    /// carried 8.3 % of the bus level - an audible click on a parameter no
    /// criterion exempts. MEASURED, on Seraphis SC-005's own automation render of
    /// ID 1 (max per-sample delta in a 20 ms window on the step, against the same
    /// statistic 64 ms clear of any step, bound 1.5 x):
    ///
    ///   20 ms -> 2.651   100 ms -> 1.143   200 ms -> 1.143
    ///   300 ms -> 1.144  500 ms -> 1.147
    ///
    /// 100 ms is the KNEE: from there on the statistic is the render's own floor
    /// and is flat to four digits, i.e. the staircase has disappeared under it
    /// and no further lengthening buys anything. Nothing about the delivery
    /// shape changes - the value is still read once and held for the chunk, so
    /// SC-014's partition invariance is untouched - and `prepare()` still SNAPS
    /// (:329), so no test that sets polyphony before prepare sees a ramp at all.
    static constexpr float kSumGainSmoothMs = 100.0f;
    /// FR-046. -30 dBFS long-release steal amnesty threshold.
    static constexpr float kAmnestyLevelThreshold = 0.0316f;
    /// FR-053. Shipped output tape-saturation amount.
    static constexpr float kOutputSaturation = 0.15f;
    /// FR-053. Output drive, not user-exposed.
    static constexpr float kOutputDriveDb = 0.0f;
    /// Plan §3.4: at most one deferred non-finite voice reset per control chunk.
    static constexpr std::size_t kResetsPerControlChunk = 1;
    /// Plan §3.8: at most one deferred freeze-capture retry per control chunk.
    static constexpr std::size_t kFreezeRetriesPerChunk = 1;
    /// DUPLICATED from effects/aether_reverb.h:1442 (`kMaxBloomResonators = 32`,
    /// used at :2398-2399 as `std::min(count, kMaxBloomResonators)`). A Layer 4
    /// constant CANNOT be named from a Layer 3 header, so it is duplicated with
    /// the citation - the same treatment the three sibling systems give
    /// kControlChunkSamples (FR-007).
    static constexpr std::size_t kBloomPartialCap = 32;

    /// FR-013 heap-free-giant guard, asserted just below the class (tasks.md
    /// T005: "record sizeof(SeraphisEngine)").
    ///
    /// MEASURED, not guessed: `sizeof(SeraphisEngine)` is **771 968 B**
    /// (753.9 KiB) - g++ 13, `-std=c++20 -O1`, repo headers, this class as
    /// shipped. Its parts, measured the same way: `voices_` is
    /// 16 x 47 616 = 761 856 B (98.7 % of the object), `TruePeakLimiter` 7 176,
    /// `VoiceAllocator` 1 344, the four 64-sample bus/voice scratch arrays 1 024,
    /// two `TapeSaturator`s 496, and ~72 B of scalars and padding.
    ///
    /// The bound is expressed AGAINST `SeraphisVoice::kVoiceSizeBound` rather
    /// than as a frozen `ceil(measured x 1.05)` byte count, because the two
    /// guards would otherwise contradict each other. `kVoiceSizeBound` is 50 048
    /// - it deliberately allows a 5 % per-toolchain padding difference in
    /// `SeraphisVoice` - and 16 slots at that size is 800 768 B, already ABOVE
    /// `ceil(771 968 x 1.05) = 810 567`... plus the ~10 KB of non-voice members,
    /// i.e. 810 768. A literal 1.05x bound recorded on this toolchain would
    /// therefore turn into a hard build break on any compiler that used the
    /// headroom the voice guard explicitly permits. The additive 64 KiB covers
    /// everything the engine adds beyond the pool with ~6x headroom on the
    /// measured 10 112 B, and the guard still fires on a 17th voice slot
    /// (+47.6 KB) or on any of FR-002's forbidden members. The per-toolchain
    /// figure for `compliance.md` is printed by
    /// SeraphisEngine_PolyphonyAndPreparation.
    static constexpr std::size_t kEngineSizeBound =
        kMaxVoices * SeraphisVoice::kVoiceSizeBound + (64u * 1024u);

    /// FR-070 lifecycle hook (plan D4/D5). Bitmasks over voice slots; consumed by
    /// the caller so it can drive AetherReverb's bloom triggers one control chunk
    /// late.
    struct BloomEvents {
        std::uint32_t noteOnMask = 0u;
        std::uint32_t noteOffMask = 0u;
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief FR-041. The ONLY allocating path; prepares ALL kMaxVoices slots.
    ///
    /// Preparing all 16 slots regardless of `cfg.polyphony` is what makes
    /// setPolyphony() allocation-free: it only changes how many slots are summed
    /// and which ones the allocator may hand out. The memory consequence is
    /// stated in the spec's RA-8 (33.6 MB @ 48 kHz at captureSeconds = 4).
    void prepare(double sampleRate, const SeraphisEngineConfig& cfg) noexcept {
        const double sr = (sampleRate > 1.0) ? sampleRate : 1.0;
        polyphony_ = std::clamp(cfg.polyphony, std::size_t{1}, kMaxVoices);
        seed_ = cfg.seed;

        // Seed BEFORE prepare: SeraphisVoice::setSeed only stores while the voice
        // is unprepared and the voice's own prepare() calls applySeeds() at its
        // step 4, which is the one point at which ContinuousBody::setSeed is
        // still configure-time legal (continuous_body.h:1117-1124).
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setSeed(deriveStreamSeed(seed_, kVoiceSaltBase + v));  // FR-050
            voices_[v].prepare(sr, cfg.voice);
        }

        allocator_.reset();
        allocator_.setAllocationMode(AllocationMode::Oldest);  // voice_allocator.h:311, FR-043
        allocator_.setStealMode(StealMode::Hard);              // :317, FR-043
        // setVoiceCount is [[nodiscard]] std::span<const VoiceEvent> (:326), NOT
        // the void the spec's Existing-components table records. A bare statement
        // discards the result (MSVC C4834 / GCC -Wunused-result) against the
        // zero-warning gate. At prepare time there is nothing to release, so
        // discarding is correct; §3.6.2 (T006) is the one caller that consumes it.
        static_cast<void>(allocator_.setVoiceCount(polyphony_));

        // FR-053 shipped constants. The setters run BEFORE prepare() so the
        // saturator's parameter smoothers are SNAPPED to them (tape_saturator.h:
        // 164-168) instead of ramping in from the ctor defaults (saturation 0.5).
        satL_.setDrive(kOutputDriveDb);       // tape_saturator.h:239
        satR_.setDrive(kOutputDriveDb);
        satL_.setSaturation(kOutputSaturation);  // :248
        satR_.setSaturation(kOutputSaturation);
        satL_.setMix(1.0f);                   // :266
        satR_.setMix(1.0f);
        // The block-size argument is ignored by TapeSaturator::prepare (:141);
        // kControlChunkSamples is passed because that is the cadence
        // processOutputStage drives it on, not because it is a limit.
        satL_.prepare(sr, kControlChunkSamples);
        satR_.prepare(sr, kControlChunkSamples);

        // FR-054. Prepared at kMaxBlockSamples; processBlock chunks internally
        // (true_peak_limiter.h:104-118) so a larger caller block cannot overrun.
        limiter_.setCeilingDb(TruePeakLimiter::kDefaultCeilingDb);  // :46, :85
        limiter_.prepare(sr, kMaxBlockSamples);                     // :59

        // FR-052. SNAPPED, not ramped: a first block that faded up from 0 over
        // kSumGainSmoothMs would be a level artefact on every prepare().
        sumGain_.configure(kSumGainSmoothMs, static_cast<float>(sr));  // smoother.h:160
        sumGain_.snapTo(sumGainForPolyphony(polyphony_));              // :263
        sumGainHeld_ = sumGain_.getCurrentValue();                     // :191

        sampleCounter_ = 0;
        nonFinitePending_ = 0u;
        // FR-072's counter is a LIFETIME diagnostic (the shape
        // AetherReverb::getNonFiniteRecoveryCount uses, aether_reverb.h:2588), so
        // reset()/silence() deliberately leave it alone; only a full
        // reconfiguration rewinds it.
        nonFiniteRecoveries_ = 0u;
        // FR-030a. A reconfiguration is not a played freeze: the latch starts
        // clear and every ring is empty, so there is nothing to arm.
        freezeLatched_ = false;
        freezePending_ = 0u;
        freezeCursor_ = 0;
        bloomOffMask_ = 0u;
        bloomOnPending_ = 0u;
        bloomOnMask_ = 0u;
        lastBloomCount_.fill(std::size_t{0});
        orphanTail_ = 0u;
        retriggerSlot_ = -1;
        stealTeardown_ = 0u;
        lastStolenVoice_ = -1;
        // FR-045 step 4's tie-break key restarts with the pool (plan §3.6.1).
        voiceSerial_.fill(std::uint64_t{0});
        nextSerial_ = 1u;
        prepared_ = true;
    }

    /// @brief FR-055. Per-voice reset() plus the output stage.
    ///
    /// @warning NOT AN AUDIO-THREAD OPERATION. SeraphisVoice::reset() reaches
    ///          AtmosphereEngine::reset() -> RollingCaptureBuffer::reset(), a
    ///          std::fill over the whole stereo capture ring
    ///          (rolling_capture_buffer.h:96-99). At the shipped
    ///          captureSeconds = 4.0f that is ~2 MiB per voice, i.e. ~32 MiB
    ///          across the pool @ 48 kHz (plan §9 R13). It is allocation-free and
    ///          lock-free, but it is not a bounded per-block cost.
    void reset() noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].reset();
        }
        clearRunState();
    }

    /// @brief FR-055. Per-voice silence() then reset() - the tail-CLEARING one
    ///        (plan D3) - plus the output stage.
    ///
    /// The second call is reset() and NEVER resetForSteal(): silence() arms an
    /// anti-click decay from the voice's last emitted sample pair, and with the
    /// tail-preserving entry point every slot would be left holding a live armed
    /// tail that the next note - whenever it arrives - would sum in as a click.
    /// With reset() the block after silence() is exactly 0 on both channels.
    ///
    /// AtmosphereEngine::silence() only sets runState_ = Silencing
    /// (atmosphere_engine.h:644-650) and keeps rendering under a 10 ms ramp, so
    /// the paired reset() is what actually clears it - which is why the pair is
    /// silence()-then-reset() and not either one alone.
    ///
    /// @warning NOT AN AUDIO-THREAD OPERATION - see reset().
    void silence() noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].silence();  // FR-034: arm + hard-clear the sub-components
            voices_[v].reset();    // FR-055: and CLEAR the armed tail
        }
        clearRunState();
    }

    /// @brief FR-040. Clamped to [1, kMaxVoices]. Allocation-free (FR-041).
    ///
    /// This is the ONLY place the FR-052 sum-gain target moves. Note events never
    /// touch it, so SC-014's 1-sample and 512-sample partitions can never update
    /// it at different times.
    void setPolyphony(std::size_t n) noexcept {
        polyphony_ = std::clamp(n, std::size_t{1}, kMaxVoices);
        // PLAN §3.6.2, AND THE SOLE WRITER OF orphanTail_.
        //
        // setVoiceCount pushes a NoteOff for every excess slot that was Active or
        // Releasing AND force-idles it in the same loop - state = Idle, note = -1,
        // velocity = 0, frequency = 0, activeVoiceCount_ decremented
        // (voice_allocator.h:340-352). The engine therefore treats the event as a
        // MUSICAL RELEASE and NOT as a retirement: voices_[i].noteOff() only, never
        // allocator_.voiceFinished(i) (the allocator has already idled the slot),
        // and the voice keeps rendering its tail because isRendering()'s second
        // clause is !isFinished() (FR-040 step 2).
        //
        // A slot that is still sounding when the shrink idles it is an ORPHAN: the
        // allocator may hand it out again at any moment, and FR-047 (Clarification
        // Q8) requires the full silence()/resetForSteal()/noteOn() teardown when it
        // does. orphanTail_ is the ONLY predicate that identifies those slots at
        // dispatch time - see the NoteOn row in dispatch().
        for (const VoiceEvent& e : allocator_.setVoiceCount(polyphony_)) {  // :326
            const std::size_t i = static_cast<std::size_t>(e.voiceIndex);
            if (i >= kMaxVoices || e.type != VoiceEvent::Type::NoteOff) {
                continue;
            }
            voices_[i].noteOff();
            if (!voices_[i].isFinished()) {
                orphanTail_ |= voiceBit(i);
            }
        }
        sumGain_.setTarget(sumGainForPolyphony(polyphony_));  // smoother.h:170
    }

    /// @brief FR-050. Re-derives every slot seed from the new engine seed.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setSeed(deriveStreamSeed(seed_, kVoiceSaltBase + v));
        }
    }

    // =========================================================================
    // Notes (FR-042)
    // =========================================================================
    //
    // Plan §3.6 / §3.6.0 / §3.6.1 / §3.6.2 in full. With the §3.6.1 selection in
    // place a saturated pool NEVER reaches the allocator's own `Oldest` steal:
    // the victim slot is freed first, so the allocator emits a plain `NoteOn`
    // and the dispatch table's `Steal` row is the defensive path, not the live
    // one.

    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
        if (!prepared_) {
            return;
        }
        if (velocity == 0u) {  // Edge Case 13; the allocator maps it too (:230-233)
            noteOff(note);
            return;
        }
        // Provenance is established BEFORE the allocator call, with the
        // allocator's own public read surface - getVoiceState (:424) and
        // getVoiceNote (:406), the predicate findVoicePlayingNote (:830-841) uses
        // internally. It is the only way to tell the two provenances of a Steal
        // event apart (plan §3.6.0), and it CANNOT be recovered afterwards: by
        // the time the returned span is walked the slot already carries the new
        // note.
        retriggerSlot_ = -1;
        for (std::size_t i = 0; i < polyphony_; ++i) {
            if (allocator_.getVoiceState(i) != VoiceState::Idle
                && allocator_.getVoiceNote(i) == static_cast<int>(note)) {
                retriggerSlot_ = static_cast<int>(i);
                break;
            }
        }
        // PLAN §3.6.1 / RA-4. The allocator has no `Quietest` mode and cannot
        // see a level (voice_allocator.h:55-60, :124-125), so selection lives
        // here and the chosen slot is freed BEFORE the allocator call. A
        // retrigger is excluded: that note already owns a slot, so nothing is
        // saturated from its point of view.
        if (retriggerSlot_ < 0 && noIdleVoice()) {
            freeChosenVictimSlot();
        }
        dispatch(allocator_.noteOn(note, velocity));  // :228
        // R6 / RA-4, asserted rather than assumed: the freed slot is the ONLY
        // idle slot the allocator can see, so the NoteOn it emitted must have
        // named it - which is what cleared the stealTeardown_ bit in dispatch().
        // A leftover bit means the allocator allocated somewhere else and the
        // FR-047 teardown never ran on the victim; that is a defect, not a
        // fallback, so it fails loudly in a debug build. SC-011 checks the same
        // thing from the outside via getLastStolenVoiceIndex().
        assert(stealTeardown_ == 0u
               && "RA-4: allocator_.noteOn did not allocate the freed victim slot");
        stealTeardown_ = 0u;
        retriggerSlot_ = -1;
    }

    void noteOff(std::uint8_t note) noexcept {
        if (!prepared_) {
            return;
        }
        retriggerSlot_ = -1;
        dispatch(allocator_.noteOff(note));  // voice_allocator.h:257
    }

    // =========================================================================
    // Rendering
    // =========================================================================

    /// @brief FR-051. Voice sum ONLY - no reverb, no output stage.
    ///
    /// The engine runs its OWN absolute control grid (the aether_reverb.h:
    /// 2181-2195 idiom) anchored to sampleCounter_, and hands the resulting
    /// slices to the voices, which absorb any caller partition through their own
    /// carry FIFOs (plan D1). The control step is SPLIT: runPreRenderControlStep
    /// sets the chunk up, runPostRenderControlStep observes what it produced -
    /// load-bearing for plan D4's bloom snapshot (T008) and for FR-044's
    /// retirement being partition-invariant.
    ///
    /// The render loop bound is `v < kMaxVoices` UNCONDITIONALLY. A bookkeeping
    /// high-water bound would leave spare slots receiving neither
    /// processStereoBlock nor advanceLifeOnly, and SC-016's "every voice" clauses
    /// are written against all 16 (plan §3.2).
    void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept {
        if (outL == nullptr || outR == nullptr) {
            return;  // FR-006 guard order
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
            const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
            if (phase == 0u) {
                runPreRenderControlStep();
            }
            const std::size_t slice = std::min(n - done, kControlChunkSamples - phase);

            std::fill_n(busL_.data(), slice, 0.0f);
            std::fill_n(busR_.data(), slice, 0.0f);
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                if (!isRendering(v)) {
                    voices_[v].advanceLifeOnly(slice);  // FR-027 / FR-051
                    continue;
                }
                voices_[v].processStereoBlock(vL_.data(), vR_.data(), slice);
                for (std::size_t s = 0; s < slice; ++s) {
                    const float a = vL_[s];
                    const float b = vR_[s];
                    if (!isFiniteBits(a) || !isFiniteBits(b)) {
                        // FR-072, AT the accumulation point. The reset is
                        // DEFERRED to runPreRenderControlStep (T008): calling
                        // voices_[v].reset() inline would let one poisoned block
                        // trigger up to 16 x ~2 MiB of capture-ring clearing
                        // inside a single 1.33 ms control chunk (plan §3.4).
                        nonFinitePending_ |= voiceBit(v);
                        break;  // this voice contributes 0 for the rest of the slice
                    }
                    busL_[s] += a;
                    busR_[s] += b;
                }
            }

            const float g = sumGainHeld_;  // FR-052, read once per control chunk
            for (std::size_t s = 0; s < slice; ++s) {
                outL[done + s] = busL_[s] * g;
                outR[done + s] = busR_[s] * g;
            }

            sampleCounter_ += slice;
            done += slice;
            if (sampleCounter_ % kControlChunkSamples == 0u) {
                runPostRenderControlStep();  // the slice that COMPLETED a chunk
            }
        }
    }

    /// @brief FR-053a. In place; the caller runs this AFTER its reverb.
    ///
    /// Calling it on a buffer the engine did not produce is the INTENDED usage -
    /// in the composed chain the buffer is the AetherReverb return.
    ///
    /// The 64-sample loop around the saturator is a CADENCE CHOICE, NOT A SIZE
    /// CONSTRAINT: TapeSaturator::prepare ignores its block-size argument
    /// (tape_saturator.h:141) and TapeSaturator::process is per-sample stateful
    /// and partition-invariant, so `satL_.process(l, n)` over the whole block
    /// would be equally correct. Phase 8 must not copy the loop as if it were a
    /// requirement. The limiter is ALWAYS LAST and takes the whole block.
    void processOutputStage(float* l, float* r, std::size_t n) noexcept {
        if (l == nullptr || r == nullptr || n == 0 || !prepared_) {
            return;  // FR-006 guard order
        }
        for (std::size_t done = 0; done < n; done += kControlChunkSamples) {
            const std::size_t slice = std::min(kControlChunkSamples, n - done);
            satL_.process(l + done, slice);  // tape_saturator.h:335 - mono, in place
            satR_.process(r + done, slice);
        }
        limiter_.processBlock(l, r, static_cast<int>(n));  // true_peak_limiter.h:104
    }

    // =========================================================================
    // Freeze / output trim
    // =========================================================================

    /// @brief FR-030a, plan §3.8. Latch the engine-wide freeze; ARM ONLY.
    ///
    /// setAtmosphereFreeze(true) does NOT capture. It raises one pending bit per
    /// slot and returns; runPreRenderControlStep() step 2 services at most
    /// kFreezeRetriesPerChunk of them per control chunk. Two reasons, both
    /// load-bearing:
    ///
    ///  1. A capture is a no-op until that voice's ring holds a whole analysis
    ///     window (atmosphere_engine.h:913-917, `capture_.getAvailableSamples()
    ///     < need`), and the ring is only written by a voice that RENDERS. A
    ///     single fan-out call at latch time would therefore leave every
    ///     cold-started slot - and every slot the FR-047 steal path has just
    ///     reset - permanently unfrozen.
    ///  2. A SUCCEEDING capture is an extractSlice plus two
    ///     SpectralFreezeOscillator::freeze calls, i.e. one FFT of
    ///     freezeFftSize (2048 by default) PER CHANNEL (:918-921). All 16 rings
    ///     fill from the same reset(), so the availability test flips for every
    ///     slot on the same chunk: an inline fan-out puts up to 16 x 2 = 32
    ///     FFT(2048) inside one caller call, and an un-staggered retry puts them
    ///     inside one 1.33 ms control chunk (plan §9 R14).
    ///
    /// Releasing is the cheap direction - AtmosphereEngine::releaseFreeze() is a
    /// state flip plus a one-hop fade arm (:928-938) - so it fans out inline.
    void setAtmosphereFreeze(bool on) noexcept {
        freezeLatched_ = on;
        if (on) {
            freezePending_ = kAllVoicesMask;
            return;
        }
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].releaseFreeze();
        }
        freezePending_ = 0u;
    }
    [[nodiscard]] bool getAtmosphereFreeze() const noexcept { return freezeLatched_; }

    /// FR-053. Exposes the saturation amount only; the drive is not user-exposed
    /// at Layer 3. TapeSaturator::setSaturation clamps to [0, 1] (:248).
    void setOutputSaturation(float amount) noexcept {
        satL_.setSaturation(amount);
        satR_.setSaturation(amount);
    }

    /// @brief FR-072. The ONLY read-back for the output-saturation amount.
    ///
    /// A PURE CONST FORWARDER - it adds NO state. setOutputSaturation (above) is
    /// the only writer of satL_/satR_ besides prepare()'s kOutputSaturation push
    /// (:230-231), and TapeSaturator already ships the read-back: saturation_ is
    /// written ONLY by setSaturation, as `std::clamp(amount, 0.0f, 1.0f)` BEFORE
    /// the smoother target (tape_saturator.h:248-252), i.e. exactly "the amount
    /// last pushed, not the saturator's ramp position". TapeSaturator::reset()
    /// snaps the smoother to saturation_ and does not clear it (:180-199), so
    /// SeraphisEngine::reset() cannot desynchronise the two either.
    ///
    /// A mirrored member would be a SECOND source of truth and two places that
    /// must stay in step - which is the divergence this accessor exists to rule
    /// out.
    ///
    /// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
    /// @par Real-Time Safety: a pure const member read - allocation-free,
    ///      lock-free, exception-free.
    [[nodiscard]] float getOutputSaturation() const noexcept {
        return satL_.getSaturation();  // tape_saturator.h:283
    }

    // =========================================================================
    // Phase 9 parameter surface (FR-001, FR-002, FR-005)
    // =========================================================================

    /// @brief FR-002 (spec Phase 9). Broadcast the run-time voice parameter set
    ///        to EVERY slot.
    ///
    /// THE BOUND IS kMaxVoices AND NOT getPolyphony(), and that is load-bearing.
    /// setPolyphony() force-idles an excess slot with voices_[i].noteOff() and
    /// records orphanTail_ |= voiceBit(i) when !isFinished() (:339-348), and
    /// processStereoBlock's loop bound is `v < kMaxVoices` unconditionally
    /// (:437, :464-486). A getPolyphony() bound would leave an audibly-summed
    /// orphan running on prepare-time defaults for its whole release - up to
    /// 8000 ms at the shipped default (seraphis_voice.h:359) - and would leave a
    /// slot the allocator hands out after a polyphony INCREASE unconfigured.
    /// Same bound as setSeed (:355) and setAtmosphereFreeze (:557).
    ///
    /// Does NOT call setSpectralState / setSpectralStateCount: those are
    /// configure-time gated and belong to applySpectralStates (FR-005).
    ///
    /// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    ///      37 noexcept scalar setters x kMaxVoices; every one is idempotent.
    void applyVoiceParams(const SeraphisVoiceParams& p) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            SeraphisVoice& voice = voices_[v];
            // Cloud
            voice.setCloudDriftSmoothness(p.cloudDriftSmoothness);   // FR-070 #1
            voice.setDecayTimeSec(p.cloudDecaySec);                  // :650
            voice.setEnvelopeOffsetSpread(p.cloudEnvOffsetSpread);   // FR-070 #2
            // Morph
            voice.setBloom(p.morphBloom);                            // :654
            voice.setTravelMode(p.morphTravelMode);                  // :664
            voice.setTravelRate(p.morphTravelRate);                  // :657
            voice.setWaypointInterval(p.morphWaypointSeconds);       // FR-070 #5
            // Spatial
            voice.setSpatialRate(p.spatialRateHz);                   // :615
            voice.setSpatialCoupling(p.spatialCoupling);             // :616
            voice.setSpatialGrowth(p.spatialGrowth);                 // :617
            // Envelope
            voice.setEnvelopeMode(p.envMode);                        // :567
            voice.setGrowthDurationSeconds(p.envGrowthDurationSec);  // :580
            // Body
            voice.setMaterial(p.bodyMaterial);                       // :673
            voice.setResonance(p.bodyResonance);                     // :674
            voice.setKeyTracking(p.bodyKeyTracking);                 // :676
            voice.setDrive(p.bodyDrive);                             // :677
            voice.setMix(p.bodyMix);                                 // :678
            voice.setCloudMix(p.bodyCloudMix);                       // :679
            voice.setCloudDecaySec(p.bodyCloudDecaySec);             // :680
            voice.setCloudSize(p.bodyCloudSize);                     // :681
            voice.setCloudDamping(p.bodyCloudDamping);               // :682
            voice.setWidth(p.bodyWidth);                             // :683
            voice.setBodyInputAgcEnabled(p.bodyInputAgc);            // FR-070 #12
            voice.setBodyResonatorBypass(p.bodyResonatorBypass);     // FR-070 #13
            // Atmosphere
            voice.setDensity(p.atmosDensity);                        // :688
            voice.setGrainSeconds(p.atmosGrainSeconds);              // :689
            voice.setPanSpread(p.atmosPanSpread);                    // :691
            voice.setDecorrelation(p.atmosDecorrelation);            // :692
            voice.setFreezeMix(p.atmosFreezeMix);                    // :693
            voice.setAtmosDriftSmoothness(p.atmosDriftSmoothness);   // FR-070 #3
            voice.setAtmosDriftRangeSemitones(p.atmosDriftRangeSemis);// FR-070 #4
            voice.setAtmosJitter(p.atmosJitter);                     // FR-070 #6
            voice.setAtmosPositionSeconds(p.atmosPositionSeconds);   // FR-070 #7
            voice.setAtmosPositionSpread(p.atmosPositionSpread);     // FR-070 #8
            voice.setAtmosPitchSemitones(p.atmosPitchSemitones);     // FR-070 #9
            voice.setAtmosPitchSpread(p.atmosPitchSpread);           // FR-070 #10
            voice.setAtmosGrainEnvelope(p.atmosGrainEnvelope);       // FR-070 #11
        }
    }

    /// @brief FR-005. Configure-time fan-out of the four spectral slots.
    ///
    /// ALL FOUR SLOTS ARE WRITTEN, not `count` of them. SpectralMorphEngine::
    /// setState accepts any slot in [0, kMaxStates) irrespective of numStates_
    /// (spectral_morph_engine.h:292-295) and stores it, so writing all four is
    /// legal - and it is REQUIRED, because SC-003's rows for IDs 411/412 raise
    /// kMorphStateCountId to 4 and then expect slots 2 and 3 to already carry
    /// their content. `states` is always the Processor's 4-slot array (FR-041b).
    ///
    /// The per-voice gate (seraphis_voice.h:770-783) is the ONLY guard; this
    /// function adds none and swallows no rejection - the caller reads
    /// getRejectedConfigureTimeCallCount() (:784) across the pool to decide
    /// whether to retry (FR-046 clause 3).
    ///
    /// kMaxVoices, not getPolyphony(): a slot the allocator hands out later must
    /// already carry the states.
    ///
    /// `voiceMask` selects which slots are written; bit v selects voices_[v].
    /// The default 0xFFFF is the whole pool, so the FR-005 contract is unchanged
    /// for every caller that does not name a mask. It exists because the FR-046
    /// RETRY must not re-push to voices that already accepted: on an accepting
    /// voice SpectralMorphEngine::setState runs isValidSpectralState AND
    /// buildSanitized - a full 64-entry std::log2 pass
    /// (spectral_morph_engine.h:296-301, :537-543) - BEFORE the identity check at
    /// :302-304 that would make it a no-op. A whole-pool retry therefore costs
    /// 15 x 4 x 64 ~= 3840 std::log2 per block, every block, for as long as ONE
    /// voice keeps rejecting - which is the whole of a sustained note plus its
    /// release (up to 8000 ms, seraphis_voice.h:359).
    ///
    /// THE SAME ARITHMETIC BOUNDS THE SUCCESS PATH: a mask of 0xFFFF over a
    /// quiescent pool is 16 x 4 = 64 buildSanitized calls = 4096 std::log2 plus
    /// 64 isValidSpectralState scans plus 64 128-float array comparisons, in ONE
    /// process() call. That is why the caller raises its pending / retry mask
    /// only when a slot id actually moved or the engine was re-prepared.
    ///
    /// The mask is a DEFAULTED PARAMETER on this same symbol, not a second
    /// overload, so no existing call site changes shape.
    ///
    /// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    void applySpectralStates(const SpectralState* states, int count,
                             std::uint16_t voiceMask = 0xFFFFu) noexcept {
        if (states == nullptr) {
            return;
        }
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            // voiceBit() (:766) rather than a uint16_t shift: its std::uint32_t
            // result keeps the whole comparison unsigned, which is what the
            // zero-warning gate needs (MSVC C4389 / GCC -Wsign-compare).
            if ((voiceMask & voiceBit(v)) == 0u) {
                continue;
            }
            voices_[v].setSpectralStateCount(count);   // clamps [2,4] downstream (:319)
            for (int slot = 0; slot < SpectralMorphEngine::kMaxStates; ++slot) {
                voices_[v].setSpectralState(slot, states[static_cast<std::size_t>(slot)]);
            }
        }
    }

    // =========================================================================
    // Layer-4 hand-off surfaces (FR-070, FR-071) - T008
    // =========================================================================

    /// @brief FR-071, plan §3.7. The held partial frequencies of `voiceIndex`,
    ///        selected for AetherReverb::bloomNoteOn. Plain floats - no Layer 4
    ///        type is named.
    ///
    /// THE SELECTION RULE IS PART OF THE CONTRACT, not an implementation detail.
    /// bloomNoteOn truncates at kMaxBloomResonators = 32 (aether_reverb.h:1442,
    /// :2377, :2398-2399) while HarmonicCloud::kMaxPartials = 64
    /// (harmonic_cloud.h:138), so up to half a full-richness voice's partials are
    /// dropped and WHICH 32 is audible:
    ///   1. rank all active partials by CURRENT AMPLITUDE descending
    ///      (harmonic_cloud.h:959), ties by lower partial index;
    ///   2. keep the first outCount = min(active, kBloomPartialCap, capacity);
    ///   3. emit them ASCENDING BY FREQUENCY (:955).
    /// Amplitude and not index, because the bloom stage is a resonant emphasis of
    /// the held chord: reinforcing 32 inaudible upper partials while dropping the
    /// fundamental's neighbours inverts the effect.
    ///
    /// Both sorts run over a 64-entry stack array with a noexcept comparator, so
    /// std::sort is allocation-free and exception-free (introsort/heapsort, no
    /// heap). It is called once per note-on inside a control step, NEVER on the
    /// per-sample path (plan §9 R12).
    void collectHeldPartials(std::size_t voiceIndex, float* dest, std::size_t capacity,
                             std::size_t& outCount) const noexcept {
        outCount = 0;
        if (dest == nullptr || capacity == 0 || voiceIndex >= kMaxVoices) {
            return;
        }
        const HarmonicCloud& cloud = voices_[voiceIndex].cloud();
        const std::size_t active =
            std::min(cloud.getActivePartialCount(), HarmonicCloud::kMaxPartials);  // :950, :138
        const std::size_t wanted = std::min({active, kBloomPartialCap, capacity});
        if (wanted == 0) {
            return;
        }

        std::array<std::uint8_t, HarmonicCloud::kMaxPartials> order{};
        for (std::size_t i = 0; i < active; ++i) {
            order[i] = static_cast<std::uint8_t>(i);
        }
        std::uint8_t* const first = order.data();
        std::sort(first, first + active,
                  [&cloud](std::uint8_t a, std::uint8_t b) noexcept {
                      const float ampA = cloud.getPartialCurrentAmplitude(a);  // :959
                      const float ampB = cloud.getPartialCurrentAmplitude(b);
                      if (ampA > ampB) {
                          return true;
                      }
                      if (ampB > ampA) {
                          return false;
                      }
                      return a < b;  // FR-071's tie-break: the LOWER partial index
                  });
        std::sort(first, first + wanted,
                  [&cloud](std::uint8_t a, std::uint8_t b) noexcept {
                      const float hzA = cloud.getPartialFrequencyHz(a);  // :955
                      const float hzB = cloud.getPartialFrequencyHz(b);
                      if (hzA < hzB) {
                          return true;
                      }
                      if (hzB < hzA) {
                          return false;
                      }
                      return a < b;
                  });
        for (std::size_t i = 0; i < wanted; ++i) {
            dest[i] = cloud.getPartialFrequencyHz(order[i]);
        }
        outCount = wanted;
    }

    /// @brief FR-070 lifecycle hook (plan D4/D5). Reading CLEARS both masks.
    ///
    /// D5: the caller polls this AFTER processStereoBlock returns and issues
    /// bloomNoteOn/bloomNoteOff before the NEXT block, so AetherReverb is driven
    /// bloomNoteOn-late by exactly one control chunk. That is the only ordering
    /// under which the partial frequencies are correct (D4: setFundamentalHz only
    /// raises the dirty flag, harmonic_cloud.h:402, and frequencyHz_[] is
    /// recomputed at the head of the next updateControl, :1656-1661), and <= 64
    /// samples on a resonant-emphasis stage is inaudible.
    [[nodiscard]] BloomEvents consumeBloomEvents() noexcept {
        const BloomEvents events{.noteOnMask = bloomOnMask_, .noteOffMask = bloomOffMask_};
        bloomOnMask_ = 0u;
        bloomOffMask_ = 0u;
        return events;
    }

    // =========================================================================
    // Introspection (FR-085)
    // =========================================================================

    [[nodiscard]] std::size_t getPolyphony() const noexcept { return polyphony_; }

    /// Slots below the current polyphony that the allocator does not report Idle.
    [[nodiscard]] std::size_t getActiveVoiceCount() const noexcept {
        std::size_t count = 0;
        for (std::size_t v = 0; v < polyphony_; ++v) {
            if (allocator_.getVoiceState(v) != VoiceState::Idle) {
                ++count;
            }
        }
        return count;
    }

    /// Slots taking the full audio path this block - exactly isRendering()'s
    /// predicate, so a post-shrink orphan tail is counted while it still rings.
    [[nodiscard]] std::size_t getRenderingVoiceCount() const noexcept {
        std::size_t count = 0;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (isRendering(v)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] float getVoiceLevel(std::size_t index) const noexcept {
        return (index < kMaxVoices) ? voices_[index].getCurrentLevel() : 0.0f;
    }
    [[nodiscard]] VoiceState getVoiceState(std::size_t index) const noexcept {
        return (index < kMaxVoices) ? allocator_.getVoiceState(index) : VoiceState::Idle;
    }
    [[nodiscard]] const SeraphisVoice& getVoice(std::size_t index) const noexcept {
        return voices_[index < kMaxVoices ? index : 0u];
    }
    /// Plan §3.6.1 step 5. The slot the last steal took, -1 before the first.
    [[nodiscard]] int getLastStolenVoiceIndex() const noexcept { return lastStolenVoice_; }
    /// FR-072. One increment per voice actually reset by the deferred recovery,
    /// i.e. per serviced bit of nonFinitePending_. A LIFETIME counter: prepare()
    /// rewinds it, reset()/silence() do not.
    [[nodiscard]] std::uint32_t getNonFiniteRecoveryCount() const noexcept {
        return nonFiniteRecoveries_;
    }
    /// FR-045 step 4's allocation order, tracked engine-side (plan §3.6.1).
    /// Strictly increasing across note events; the steal tie-break key. Added
    /// beyond FR-085's enumerated list - plan §10 V-9.
    ///
    /// Engine-owned because the allocator's own `timestamp` is a member of its
    /// PRIVATE internal voice struct (voice_allocator.h:483, `private:` at :471)
    /// and "lower voice index" is not equivalent - the allocator's Oldest walk
    /// ranks by timestamp (:575-576) and only falls back to first-index on an
    /// exact tie. 0 means "never allocated".
    [[nodiscard]] std::uint64_t getVoiceAllocationSerial(std::size_t index) const noexcept {
        return (index < kMaxVoices) ? voiceSerial_[index] : std::uint64_t{0};
    }
    [[nodiscard]] std::uint32_t getSeed() const noexcept { return seed_; }
    /// FR-085 / SC-017. EXACTLY the array collectHeldPartials last produced for
    /// that slot - the one a caller hands to AetherReverb::bloomNoteOn - which is
    /// the only way a test can compare what was SENT against the cloud's
    /// partials, since AetherReverb has no read-back for it.
    [[nodiscard]] std::span<const float> getLastBloomPartials(std::size_t index) const noexcept {
        if (index >= kMaxVoices) {
            return {};
        }
        return std::span<const float>(lastBloomPartials_[index].data(), lastBloomCount_[index]);
    }
    [[nodiscard]] std::size_t getLastBloomCount(std::size_t index) const noexcept {
        return (index < kMaxVoices) ? lastBloomCount_[index] : std::size_t{0};
    }

private:
    /// Plan §4.4: the macro matrix needs NON-const voice access, which FR-085's
    /// const getVoice() deliberately is not. Declared here so T013 never has to
    /// edit this header.
    friend class SeraphisMacroMatrix;
    /// FR-072 fault injection - see the declaration above the class.
    friend struct detail::SeraphisEngineNonFiniteProbe;

    /// Every slot armed. FR-030a fans out to ALL kMaxVoices, not polyphony_:
    /// FR-041 prepares all 16, and a slot the allocator hands out later must
    /// already be pending rather than start unfrozen.
    static constexpr std::uint32_t kAllVoicesMask =
        (std::uint32_t{1} << static_cast<unsigned>(kMaxVoices)) - 1u;

    // =========================================================================
    // Helpers
    // =========================================================================

    /// Plan §3.4. NEVER std::isnan / std::isinf / std::isfinite: the macOS leg
    /// builds with -ffast-math, which licenses the compiler to fold them away.
    /// Copied verbatim from continuous_body.h:1346-1351. A plain, inlinable
    /// static - NOT the ITERUM_NOINLINE wrapper, whose own header forbids
    /// per-sample use (atmosphere_engine.h:1203-1206) - and NOT SeraphisVoice's
    /// private copy, which is not reachable from here.
    [[nodiscard]] static bool isFiniteBits(float v) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        return (bits & 0x7F800000u) != 0x7F800000u;
    }

    /// One-hot mask for slot `v`. kMaxVoices is 16, so the shift is always in
    /// range for the 32-bit masks.
    [[nodiscard]] static constexpr std::uint32_t voiceBit(std::size_t v) noexcept {
        return static_cast<std::uint32_t>(1u) << static_cast<std::uint32_t>(v);
    }

    /// FR-052's law. At polyphony 1 this is exactly 1 (Edge Case 7).
    [[nodiscard]] static float sumGainForPolyphony(std::size_t n) noexcept {
        return 1.0f / std::sqrt(static_cast<float>(n));
    }

    /// Lowest set slot of `mask`, or -1. Linear over kMaxVoices = 16 rather than
    /// a countr_zero intrinsic: it runs at most kResetsPerControlChunk times per
    /// control chunk, and the mask is never wider than the pool.
    [[nodiscard]] static int lowestSetVoice(std::uint32_t mask) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if ((mask & voiceBit(v)) != 0u) {
                return static_cast<int>(v);
            }
        }
        return -1;
    }

    /// FR-030a's round-robin: the next pending freeze slot at or after
    /// freezeCursor_, wrapping. Round-robin and not "lowest set bit": with a
    /// plain lowest-bit walk, slot 0 would be re-serviced on every chunk until it
    /// captured, and a slot whose ring fills later (a stolen one) would be
    /// starved behind it.
    [[nodiscard]] int nextPendingFreezeSlot() const noexcept {
        for (std::size_t step = 0; step < kMaxVoices; ++step) {
            const std::size_t v = (freezeCursor_ + step) % kMaxVoices;
            if ((freezePending_ & voiceBit(v)) != 0u) {
                return static_cast<int>(v);
            }
        }
        return -1;
    }

    /// Plan §3.4. The second clause is what keeps a post-shrink orphan tail
    /// rendering (FR-040 step 2); both clauses depend on SeraphisVoice seeding
    /// quiescentChunks_ at the retire value (seraphis_voice.h:864-868), without
    /// which every never-rendered slot would take the full audio path.
    [[nodiscard]] bool isRendering(std::size_t v) const noexcept {
        if (v < polyphony_) {
            return allocator_.getVoiceState(v) != VoiceState::Idle || !voices_[v].isFinished();
        }
        return !voices_[v].isFinished();
    }

    /// Plan §3.4 steps 1-3, at phase 0, BEFORE the voices render the chunk.
    void runPreRenderControlStep() noexcept {
        // 1. Sum gain, read once and held for the whole chunk, so the value is
        //    identical under any caller partition (FR-052, SC-014). The `- 1` is
        //    load-bearing: OnePoleSmoother::process() itself advances one sample
        //    (smoother.h:197-210), so advanceSamples(64) + process() would
        //    advance 65 samples per 64-sample chunk.
        sumGain_.advanceSamples(kControlChunkSamples - 1);  // smoother.h:243
        sumGainHeld_ = sumGain_.process();                  // :197

        // 2. Freeze-pending retry (FR-030a, plan §3.4 step 2), STAGGERED to at
        //    most kFreezeRetriesPerChunk slots per chunk, round-robined from the
        //    last serviced index.
        //
        //    The FAILING branch is nearly free - captureFreeze() early-outs
        //    until the ring holds a whole window (atmosphere_engine.h:913-917).
        //    The SUCCEEDING branch is two FFT(2048)s (:918-921), and all 16
        //    rings fill from the same reset(), so the availability test flips
        //    for every slot on the SAME chunk. Un-staggered that is 32
        //    FFT(2048) inside one 1.33 ms chunk; staggered it is at most 2, and
        //    the whole pool is captured within ~16 chunks (~21 ms) - four orders
        //    of magnitude inside FR-030a's ">= captureSeconds" observable.
        for (std::size_t serviced = 0; freezeLatched_ && serviced < kFreezeRetriesPerChunk;
             ++serviced) {
            const int slot = nextPendingFreezeSlot();
            if (slot < 0) {
                break;
            }
            const auto v = static_cast<std::size_t>(slot);
            if (voices_[v].isFreezeCaptured()) {
                freezePending_ &= ~voiceBit(v);  // done with this slot
            } else {
                voices_[v].captureFreeze();  // may still be a documented no-op
            }
            freezeCursor_ = (v + 1u) % kMaxVoices;
        }

        // 3. Non-finite recovery (FR-072, plan §3.4 step 3): at most
        //    kResetsPerControlChunk bits of nonFinitePending_.
        //
        //    reset() and NOT resetForSteal(): a poisoned voice must not carry a
        //    poisoned D3 fade tail forward into the next note. The bound exists
        //    because each reset reaches AtmosphereEngine::reset() ->
        //    RollingCaptureBuffer::reset(), a std::fill over the whole stereo
        //    capture ring (:527; rolling_capture_buffer.h:96-99) - ~2 MiB at the
        //    shipped captureSeconds = 4. Servicing every pending bit inline
        //    would put 16 of them (32 MiB) inside one 1.33 ms chunk (plan R13).
        for (std::size_t serviced = 0; serviced < kResetsPerControlChunk; ++serviced) {
            const int slot = lowestSetVoice(nonFinitePending_);
            if (slot < 0) {
                break;
            }
            const auto v = static_cast<std::size_t>(slot);
            nonFinitePending_ &= ~voiceBit(v);
            voices_[v].reset();
            ++nonFiniteRecoveries_;
            // FR-030a: the reset emptied that voice's capture ring and cleared
            // its freeze oscillator (atmosphere_engine.h:525-590), so a latched
            // freeze has to be re-armed for it or the slot stays unfrozen for
            // the rest of the performance.
            if (freezeLatched_) {
                freezePending_ |= voiceBit(v);
            }
        }
    }

    /// Plan §3.4 steps 4-5, AFTER the slice that completed a chunk.
    void runPostRenderControlStep() noexcept {
        // 4. Bloom collection (plan D4). The snapshot is taken HERE, in the
        //    POST-render half, and ONLY for a slot whose
        //    hasRenderedSinceNoteOn() is true - a slot whose flag is still false
        //    keeps its pending bit and is snapshotted at a later boundary.
        //
        //    That flag is the only OBSERVABLE proof that the voice's
        //    cloud_.updateControl has consumed freqDirty_:
        //    SeraphisVoice::noteOn calls cloud_.setFundamentalHz, which merely
        //    calls markFreqDirty() (harmonic_cloud.h:402); frequencyHz_[] is
        //    recomputed only at the head of updateControl (:1656-1661); and
        //    getPartialFrequencyHz returns frequencyHz_[i] verbatim (:955-957).
        //    Snapshotting any earlier - in the pre-render half, or straight out
        //    of noteOn - records the PREVIOUS note's partials, every time.
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            const std::uint32_t bit = voiceBit(v);
            if ((bloomOnPending_ & bit) == 0u || !voices_[v].hasRenderedSinceNoteOn()) {
                continue;
            }
            collectHeldPartials(v, lastBloomPartials_[v].data(), kBloomPartialCap,
                                lastBloomCount_[v]);
            bloomOnMask_ |= bit;
            bloomOnPending_ &= ~bit;
        }

        //    The ORPHAN-TAIL half of step 4: a post-shrink orphan that has rung
        //    itself out is no longer a slot the FR-047 teardown has anything to
        //    tear down, so the bit is
        //    dropped and the next note-on onto it takes the plain path. Without
        //    this the bit would survive for the lifetime of the engine and every
        //    later reuse of that slot would pay a ~2 MiB capture-ring wipe.
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if ((orphanTail_ & voiceBit(v)) != 0u && voices_[v].isFinished()) {
                orphanTail_ &= ~voiceBit(v);
            }
        }
        // 5. Deferred retirement (FR-044). Only after SeraphisVoice::isFinished()
        //    is true, which is the deferred discipline at
        //    poly_synth_engine.h:810-813. Running it on the ABSOLUTE control grid
        //    rather than "once per block" (FR-044's wording) makes retirement
        //    timing partition-invariant (plan §10 V-2); the audible effect is nil
        //    (the voice is below -100 dBFS) but the state trace SC-012 reads
        //    becomes deterministic.
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (allocator_.getVoiceState(v) == VoiceState::Releasing  // :424
                && voices_[v].isFinished()) {
                allocator_.voiceFinished(v);  // :288
                bloomOffMask_ |= voiceBit(v);
            }
        }
    }

    /// Shared by reset() and silence(): the output stage plus the engine's own
    /// run state (the FR-007 grid anchor, the held sum gain, the FR-072 pending
    /// mask), so a reset engine renders exactly as a freshly prepared one.
    ///
    /// The saturator's reset() snaps its parameter smoothers to the CURRENT
    /// values rather than re-defaulting them (tape_saturator.h:180-198), so a
    /// setOutputSaturation() the caller made survives - which is the
    /// sub-component reset contract the whole phase inherits ("clear state,
    /// leave every parameter unchanged").
    void clearRunState() noexcept {
        satL_.reset();
        satR_.reset();
        limiter_.reset();
        sumGain_.snapTo(sumGainForPolyphony(polyphony_));
        sumGainHeld_ = sumGain_.getCurrentValue();
        sampleCounter_ = 0;
        nonFinitePending_ = 0u;
        // Every voice has just been reset(), so no slot is carrying a tail an
        // FR-047 teardown could still be needed for and no note-on is waiting to
        // be snapshotted. voiceSerial_/nextSerial_ are deliberately NOT rewound:
        // they are allocation ORDER, and SC-011 clause 5 asserts they are
        // strictly increasing across note events for the life of the engine.
        orphanTail_ = 0u;
        bloomOnPending_ = 0u;
        // No note is waiting to be announced any more, and the snapshots describe
        // partials no voice is still playing. bloomOffMask_ is deliberately NOT
        // cleared: a caller that had live blooms still has to hear about their
        // ends, and reading it is what clears it (consumeBloomEvents).
        bloomOnMask_ = 0u;
        lastBloomCount_.fill(std::size_t{0});
        // FR-030a: reset() and silence() empty every capture ring and clear every
        // freeze oscillator (AtmosphereEngine::reset(), atmosphere_engine.h:
        // 525-590), so a LATCHED freeze must be re-armed across the whole pool.
        // The latch itself is a played state and survives - only the captures do
        // not.
        freezePending_ = freezeLatched_ ? kAllVoicesMask : 0u;
        freezeCursor_ = 0;
        retriggerSlot_ = -1;
        // No note-on is in flight, so no teardown can be owed (plan §3.6.1).
        // lastStolenVoice_ goes with it: after reset()/silence() no slot is
        // carrying the stolen voice's tail any more, so reporting one would
        // outlive the state it describes.
        stealTeardown_ = 0u;
        lastStolenVoice_ = -1;
    }

    /// Plan §3.6.1's saturation predicate, stated against the ALLOCATOR's view
    /// of the pool rather than the engine's: `allocateNote` searches
    /// `findIdleVoice()` over `i < voiceCount_` first and only steals when that
    /// search fails (voice_allocator.h:928-937), so an idle slot means the
    /// allocator will use it and no selection is needed. A slot that is Idle but
    /// still ringing (a post-shrink orphan) counts as idle HERE for exactly the
    /// same reason - the allocator will hand it out - and the FR-047 teardown
    /// then runs off orphanTail_ instead.
    [[nodiscard]] bool noIdleVoice() const noexcept {
        for (std::size_t i = 0; i < polyphony_; ++i) {
            if (allocator_.getVoiceState(i) == VoiceState::Idle) {  // :424
                return false;
            }
        }
        return true;
    }

    /// @brief FR-045 / FR-046 / RA-4, plan §3.6.1. Select the steal victim and
    ///        free its slot with the allocator's own public surface.
    ///
    /// `VoiceAllocator` has no `Quietest` mode (`voice_allocator.h:55-60` offers
    /// RoundRobin/Oldest/LowestVelocity/HighestNote) and "[d]oes NOT own or
    /// process any DSP" (`:124-125`), so it cannot see a level and there is no
    /// pre-emption hook on noteOn. The engine therefore selects, then frees the
    /// slot so that the allocator's own idle search has exactly one candidate.
    ///
    /// Selection, in three passes (FR-045 steps 1-4 + FR-046's amnesty):
    ///   pass 0  Releasing AND below kAmnestyLevelThreshold  -- the amnesty band
    ///   pass 1  Releasing, whatever the level               -- Edge Case 15
    ///   pass 2  Active
    /// The first pass that finds anything wins; within a pass the lowest
    /// getCurrentLevel() wins, and an exact tie goes to the LOWER voiceSerial_,
    /// i.e. the older allocation (FR-045 step 4).
    ///
    /// Pass 1 is not redundant even though `argmin` over a superset would give
    /// the same slot: it is the branch that makes "every candidate is at or
    /// above the threshold" still steal the quietest Releasing voice instead of
    /// falling through to pass 2 (which, with no Active voice in the pool, would
    /// steal nothing at all). SC-011 clause 4 is written against exactly that.
    ///
    /// The tie-break key is ENGINE-owned: VoiceAllocator's `timestamp` is a
    /// member of its private internal voice struct (voice_allocator.h:483,
    /// `private:` at :471), and "lower voice index" is NOT equivalent - the
    /// allocator's Oldest walk ranks by timestamp (:575-576) and only falls back
    /// to first-index on an exact tie.
    ///
    /// @return true when a victim was found and freed.
    bool freeChosenVictimSlot() noexcept {
        std::size_t victim = 0;
        float bestLevel = 0.0f;
        bool found = false;
        for (int pass = 0; pass < 3 && !found; ++pass) {
            for (std::size_t i = 0; i < polyphony_; ++i) {
                const VoiceState state = allocator_.getVoiceState(i);  // :424
                const float level = voices_[i].getCurrentLevel();      // FR-033
                if (pass == 2) {
                    if (state != VoiceState::Active) {
                        continue;
                    }
                } else {
                    if (state != VoiceState::Releasing) {
                        continue;
                    }
                    // `!(level < threshold)` and not `level >= threshold`: a
                    // non-finite level (which FR-072 contains but does not make
                    // impossible) must be treated as NOT eligible rather than as
                    // the quietest voice in the pool.
                    if (pass == 0 && !(level < kAmnestyLevelThreshold)) {
                        continue;
                    }
                }
                const bool better =
                    !found || level < bestLevel
                    || (level == bestLevel && voiceSerial_[i] < voiceSerial_[victim]);
                if (better) {
                    victim = i;
                    bestLevel = level;
                    found = true;
                }
            }
        }
        if (!found) {
            // Unreachable behind noIdleVoice(): a slot that is neither Idle nor
            // Releasing nor Active does not exist. Stated as a guard rather than
            // an assert so a future VoiceState addition degrades to "let the
            // allocator steal by itself" (the dispatch table's defensive Steal
            // row) instead of freeing slot 0 by accident.
            return false;
        }

        // Step 2. An Active victim has to reach Releasing before voiceFinished()
        // will touch it (:288-292 early-outs on anything else). The events are
        // DISCARDED: this is bookkeeping, not a musical release - the voice is
        // about to be silenced by the FR-047 teardown, and dispatching a NoteOff
        // to it would call voices_[victim].noteOff() one step before that.
        if (allocator_.getVoiceState(victim) == VoiceState::Active) {
            const int victimNote = allocator_.getVoiceNote(victim);  // :406
            if (victimNote >= 0) {
                static_cast<void>(
                    allocator_.noteOff(static_cast<std::uint8_t>(victimNote)));  // :257
            }
        }
        // Step 3. Now legal, and it is what returns the slot to Idle: note = -1,
        // velocity = 0, frequency = 0, activeVoiceCount_ decremented.
        allocator_.voiceFinished(victim);  // :288
        // Step 4. FR-071's "when the voice is STOLEN or finished" half lives
        // HERE, not in §3.6's Steal row: that row is unreachable under this
        // mechanism, and runPostRenderControlStep step 5 will never fire for the
        // slot either because the allocator has already idled it. Without this
        // line bloomNoteOff is never issued for a stolen voice and the reverb
        // keeps a bloom voice bound to a note that no longer exists.
        bloomOffMask_ |= voiceBit(victim);
        // ...and the FR-047 teardown, which the dispatch NoteOn row runs when the
        // allocator hands the incoming note back to this slot.
        stealTeardown_ |= voiceBit(victim);
        // Step 5.
        lastStolenVoice_ = static_cast<int>(victim);
        return true;
    }

    /// FR-047's three-step teardown, in the one order the criteria pin.
    ///
    /// silence() arms the D3 anti-click decay from the last sample the voice
    /// actually emitted and hard-clears every sub-component (seraphis_voice.h:401);
    /// resetForSteal() - NEVER reset() - then restores run state while PRESERVING
    /// that armed tail (:384), which is the whole reason the voice carries two
    /// reset entry points; noteOn() starts the incoming note. All three run
    /// synchronously inside SeraphisEngine::noteOn, i.e. inside one block.
    void teardownAndStart(std::size_t i, float frequencyHz, float velocity) noexcept {
        voices_[i].silence();
        voices_[i].resetForSteal();
        voices_[i].noteOn(frequencyHz, velocity);
        // FR-030a: both calls above reach AtmosphereEngine::reset(), which empties
        // the capture ring and clears the freeze oscillator
        // (atmosphere_engine.h:525, :584-590), so a stolen slot loses its capture
        // and has to re-arm under a latched freeze - spec FR-030a's "a steal
        // re-arms the flag for that voice", and the reason the fan-out is a
        // per-chunk retry rather than a one-shot.
        if (freezeLatched_) {
            freezePending_ |= voiceBit(i);
        }
    }

    /// Plan §3.6's dispatch table. Follows PolySynthEngine::dispatchPolyNoteOn
    /// (poly_synth_engine.h:597-620), with the Steal row SPLIT BY PROVENANCE.
    ///
    /// `served` is what makes the plan's "exactly once per dispatched span"
    /// literal: the allocator's steal path pushes Steal and NoteOn for the SAME
    /// slot in one span (voice_allocator.h:1025-1042 then :1060-1066), and two
    /// bumps would corrupt the FR-045 step 4 tie-break key that SC-011 clause 5
    /// asserts is strictly increasing in note-on order.
    void dispatch(std::span<const VoiceEvent> events) noexcept {
        std::uint32_t served = 0u;
        for (const VoiceEvent& e : events) {
            const std::size_t i = static_cast<std::size_t>(e.voiceIndex);
            if (i >= kMaxVoices) {
                continue;
            }
            const std::uint32_t bit = voiceBit(i);
            const float velocity = static_cast<float>(e.velocity) / 127.0f;
            switch (e.type) {
                case VoiceEvent::Type::NoteOn:
                    if (((orphanTail_ | stealTeardown_) & bit) != 0u) {
                        // TWO provenances, one teardown.
                        //
                        // orphanTail_: the slot a polyphony shrink force-idled
                        // while it was still sounding (§3.6.2). Clarification Q8
                        // requires the FR-047 teardown here and ONLY here on the
                        // NoteOn row: `!isFinished()` alone would also match
                        // every live retrigger target and re-introduce the
                        // §3.6.0 defect, and `allocator_.getVoiceState(i) ==
                        // Idle` is dead code (allocateNote stores Active at
                        // :933-935, before it pushes the event at :1062).
                        //
                        // stealTeardown_: the slot §3.6.1 just freed. Because
                        // that happens BEFORE allocator_.noteOn, the allocator
                        // sees an idle slot and emits a plain NoteOn, so the
                        // Steal row below never fires on a real steal - and
                        // FR-047 still demands silence() -> resetForSteal() ->
                        // noteOn() on the victim, inside this same block. Plan
                        // §3.6.1's step list stops at the allocator bookkeeping
                        // and does not restate that; without this bit a stolen
                        // voice would be layered on top of the victim's tail and
                        // SeraphisEngine_StealTeardownOrder would fail.
                        teardownAndStart(i, e.frequency, velocity);
                        orphanTail_ &= ~bit;
                        stealTeardown_ &= ~bit;
                    } else {
                        voices_[i].noteOn(e.frequency, velocity);
                    }
                    bloomOnPending_ |= bit;  // plan D4; T008 consumes it
                    if ((served & bit) == 0u) {
                        voiceSerial_[i] = nextSerial_++;
                        served |= bit;
                    }
                    break;
                case VoiceEvent::Type::NoteOff:
                    voices_[i].noteOff();
                    break;
                case VoiceEvent::Type::Steal:
                    // Plan §3.6.0: the allocator emits Steal for the OUTGOING note
                    // of an ordinary same-note retrigger too (voice_allocator.h:
                    // 239-242, :846-853), with no pool saturation involved. On
                    // that provenance the event is pure bookkeeping and the NoteOn
                    // that follows it in the same span does the work; running the
                    // FR-047 teardown here would wipe a live, sounding voice,
                    // call voices_[i].noteOn() twice and bump the serial twice.
                    if (static_cast<int>(i) == retriggerSlot_) {
                        break;  // ignore the event ENTIRELY
                    }
                    // Engine-initiated steal. With §3.6.1 in place this branch is
                    // unreachable by construction - the victim slot is freed
                    // BEFORE allocator_.noteOn, so the allocator emits a plain
                    // NoteOn and the teardown runs off stealTeardown_ on the row
                    // above. Retained as the defensive path: if a future
                    // allocator change did steal here, FR-047 would still hold
                    // (and noteOn()'s assert would name the divergence).
                    teardownAndStart(i, e.frequency, velocity);
                    bloomOnPending_ |= bit;
                    bloomOffMask_ |= bit;  // FR-071's stolen half
                    if ((served & bit) == 0u) {
                        voiceSerial_[i] = nextSerial_++;
                        served |= bit;
                    }
                    break;
            }
        }
    }

    // =========================================================================
    // State (plan §3.2). Members the T006-T008 tasks own are declared with them,
    // not here, so no field sits unused (clang's -Wunused-private-field is on
    // under -Wall).
    // =========================================================================

    std::array<SeraphisVoice, kMaxVoices> voices_;
    VoiceAllocator allocator_;

    // Per-slice accumulation - 64 samples, NOT kMaxBlockSamples (plan §10 V-1).
    std::array<float, kControlChunkSamples> busL_{};
    std::array<float, kControlChunkSamples> busR_{};
    std::array<float, kControlChunkSamples> vL_{};
    std::array<float, kControlChunkSamples> vR_{};

    TapeSaturator satL_;    // mono, in place (tape_saturator.h:335)
    TapeSaturator satR_;
    TruePeakLimiter limiter_;  // stereo, in place (true_peak_limiter.h:104)

    OnePoleSmoother sumGain_;      // FR-052, target 1/sqrt(polyphony)
    float sumGainHeld_ = 1.0f;     // read once per control chunk (plan §3.4)
    std::uint64_t sampleCounter_ = 0;  // FR-007 absolute grid
    std::size_t polyphony_ = 8;
    std::uint32_t seed_ = 1u;
    bool prepared_ = false;
    std::uint32_t nonFinitePending_ = 0u;  // FR-072 deferred-reset bitmask
    std::uint32_t bloomOffMask_ = 0u;      // FR-071's stolen/finished half
    std::uint32_t bloomOnPending_ = 0u;    // plan D4: note-ons awaiting a snapshot
    /// Plan D4/D5. Slots whose snapshot has been taken and not yet consumed.
    std::uint32_t bloomOnMask_ = 0u;
    /// FR-085 / SC-017. The exact arrays collectHeldPartials produced, per slot.
    /// 16 x 32 x 4 B = 2 KB, priced in kEngineSizeBound.
    std::array<std::array<float, kBloomPartialCap>, kMaxVoices> lastBloomPartials_{};
    std::array<std::size_t, kMaxVoices> lastBloomCount_{};

    /// FR-030a. The latched engine-wide freeze state, its per-voice retry mask
    /// and the round-robin cursor into that mask.
    bool freezeLatched_ = false;
    std::uint32_t freezePending_ = 0u;
    std::size_t freezeCursor_ = 0;

    /// FR-072. One per voice actually reset by the deferred recovery. A lifetime
    /// diagnostic: only prepare() rewinds it.
    std::uint32_t nonFiniteRecoveries_ = 0u;

    /// Plan §3.6.2. Slots a polyphony shrink force-idled while they were still
    /// sounding. THE FR-047 teardown predicate - written only by setPolyphony(),
    /// cleared by the NoteOn row and by runPostRenderControlStep step 4.
    std::uint32_t orphanTail_ = 0u;
    /// Plan §3.6. The slot the CURRENT noteOn() is a same-note retrigger of, or
    /// -1. Live only for the duration of one dispatch; it is what splits the
    /// allocator's two Steal provenances (§3.6.0).
    int retriggerSlot_ = -1;

    /// FR-045 step 4's tie-break key. Bumped exactly once per dispatched span
    /// that lands a note on the slot.
    std::array<std::uint64_t, kMaxVoices> voiceSerial_{};
    std::uint64_t nextSerial_ = 1u;

    /// Plan §3.6.1 step 5. The slot the LAST steal took, or -1 if the engine has
    /// never stolen. Exposed by getLastStolenVoiceIndex() so SC-011 can assert
    /// the RA-4 hand-off from the outside.
    int lastStolenVoice_ = -1;
    /// The victim slot §3.6.1 freed for the note-on currently being dispatched.
    /// Set by freeChosenVictimSlot(), consumed by dispatch()'s NoteOn row (which
    /// is where FR-047's teardown runs), and asserted back to 0 at the end of
    /// noteOn(). Live for the duration of ONE noteOn() call only - a bit that
    /// survives it is the RA-4 / R6 defect.
    std::uint32_t stealTeardown_ = 0u;
};

// FR-013's heap-free-giant guard. See kEngineSizeBound for what the number is
// and why it is expressed against SeraphisVoice::kVoiceSizeBound.
static_assert(sizeof(SeraphisEngine) <= SeraphisEngine::kEngineSizeBound,
              "SeraphisEngine grew past its predicted bound - check for a 17th voice slot or "
              "one of FR-002's forbidden members, and re-record kEngineSizeBound if the growth "
              "is legitimate");
static_assert(SeraphisEngine::kVoiceSaltBase > SeraphisVoice::kOrbitSalt,
              "FR-050: the engine's per-voice salt range must be disjoint from every "
              "SeraphisVoice sub-component salt");

}  // namespace DSP
}  // namespace Krate
