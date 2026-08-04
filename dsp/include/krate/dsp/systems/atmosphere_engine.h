// ==============================================================================
// Layer 3: System - AtmosphereEngine (Granular Atmosphere Engine)
// ==============================================================================
// Seraphis Phase 5. Spec slug: seraphis-phase5-atmosphere.
//   Spec:    specs/seraphis-phase5-atmosphere/spec.md
//   Plan:    specs/seraphis-phase5-atmosphere/plan.md
//   Roadmap: specs/Seraphis-roadmap.md, Part A -> Phase 5 (lines 227-248)
//
// The third generator: frozen moments and cloud particles. A parallel texture
// layer that captures the voice's own output into a rolling ring and suspends
// it - ultra-long grains (50 ms .. 30 s) read out of that ring, optionally
// smeared by a per-channel STFT phase-randomisation stage ("blur") and mixed
// against a pure-freeze drone from SpectralFreezeOscillator.
//
// LAYER DISCIPLINE (FR-002). Layer 3 may include Layer 0/1/2 only. This header
//   includes core/, primitives/ and processors/ headers exclusively; never a
//   systems/ or effects/ header. Deliberately ABSENT and not to be re-added:
//     - core/stereo_utils.h      nothing in it is used; its only function is
//                                stereoCrossBlend, a width/ping-pong blend that
//                                cannot decorrelate. There is no width control
//                                here - global width is Phase 7's StereoField.
//     - processors/brownian_drift.h  the OU recurrence is reproduced as SoA
//                                lanes (GrainDriftLanes); no BrownianDrift is
//                                instantiated. harmonic_cloud.h omits it for
//                                the same reason.
//   node tools/lint-layers.js gates this, not inspection.
//
// REAL-TIME SAFETY CONTRACT (FR-003).
//   prepare(double, const PrepareConfig&) is the ONLY method that allocates,
//   and the only method that is not real-time safe. Everything else - reset(),
//   silence(), processStereoBlock(), every setter and every accessor - is
//   noexcept, allocation-free, lock-free and I/O-free. reset() returns the exact
//   post-prepare state without deallocating. setGrainEnvelope() SELECTS one of
//   the six windows prepare() generated (see regenerateEnvelopeBank()) - it is
//   a store, safe to drive from an automation lane at block rate.
//
// MEMORY (FR-073, plan RA-2). The rolling capture ring dominates: it is stereo
//   float and its capacity is rounded UP to a power of two
//   (rolling_capture_buffer.h:83), so
//       bytes = nextPowerOf2(sampleRate * captureSeconds) * 2 channels * 4 B
//   Per voice, at 48 kHz:
//       captureSeconds  4 s -> 262144 frames ->  2.10 MB
//       captureSeconds  8 s -> 524288 frames ->  4.19 MB   (PrepareConfig default)
//       captureSeconds 16 s -> 1048576 frames -> 8.39 MB
//       captureSeconds 30 s -> 2097152 frames -> 16.8 MB
//   At 96 kHz every figure DOUBLES (30 s -> 33.6 MB per voice). Polyphony
//   multiplies it: 16 voices at 30 s / 48 kHz is 268 MB. The shipped value, and
//   any move to a ring shared between voices, is a Phase 7 decision - the table
//   is carried here so that decision is not taken by surprise.
//
//   The only other non-trivial allocation is the envelope BANK:
//   kEnvelopeTypeCount * kEnvelopeTableSize * 4 B = 98 KiB per voice, 2.3 % of
//   the default ring. It buys the removal of a 4096-entry, two-transcendental-
//   per-entry regeneration from the audio path - see regenerateEnvelopeBank().
//
// OPERATING RULE (FR-022, FR-073).
//       density (grains/s) * grainSeconds <= kMaxGrains
//   is the region in which no trigger is ever skipped for a full pool. The
//   default control table sits at 4 grains/s * 4 s = 16 concurrent grains,
//   well inside it. Past that boundary the engine SKIPS triggers - it never
//   steals a live grain (FR-023) - and getSkippedTriggerCountPoolFull() climbs.
//
// kMaxGrains = 64 IS PROVISIONAL AND MEASUREMENT-BACKED (FR-022), AND IT HAS
//   NOW BEEN MEASURED. FR-022's arithmetic ceiling is
//   106,667 ns / (64 x 512) = 3.255 ns per grain-sample.
//   AtmosphereEngine_GrainSampleCost measures (13th Gen Core i9-13900HX,
//   MSVC Release, best-of-25 x 500 blocks, three consecutive runs):
//       captureSeconds = 30 (16.8 MB ring) : 9.55 .. 9.96 ns  (2.93 .. 3.06 x)
//       captureSeconds =  8 (4.19 MB ring) : 9.38 .. 10.32 ns (2.88 .. 3.17 x)
//   THE MEMORY HYPOTHESIS DID NOT SURVIVE THAT MEASUREMENT: the two ring sizes
//   differ by 4x in bytes and by less than the run-to-run spread in cost, and a
//   probe at captureSeconds = 1 (a 256 KB/channel, L2-resident ring) also
//   measured within noise of the 8 s figure. The cost is instruction-bound, not
//   miss-bound, so there is no cache-shaped saving to find and a smaller pool
//   does not make a grain-sample cheaper - it only makes fewer of them.
//
//   kMaxGrains IS DELIBERATELY LEFT AT 64. SC-004 lever (5) exists to be spent
//   when reducing the pool brings a configuration under budget, and measurement
//   says it does not: four of SC-004's five configurations run at the FR-009
//   default of 16 concurrent grains, where the cap does not bind at all, and the
//   fifth would still land at ~99,900 ns/block at kMaxGrains = 16 - while
//   making the DEFAULT control table permanently pool-saturated. Spending a
//   capability lever that buys no criterion is not "reduce cost". The full
//   measurement, the levers that WERE spent, and the escalation this implies are
//   in the T019 decision record in
//   dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp and in
//   specs/seraphis-phase5-atmosphere/compliance.md.
//
// BLUR LATENCY (plan RA-3). With blurEnabled the whole layer - both crossfade
//   legs, because the freeze leg is delay-matched to the same figure - is
//   latent by exactly blurFftSize (the SNAPPED value), reported by
//   getLatencySamples(). It is constant between prepare() calls and does not
//   move with the blur knob. With blurEnabled = false the latency is 0.
//
// ENVELOPE ENDPOINTS AND THEIR CONDITIONING (FR-027). The forced
//   table entry 0 and the FR-026 phase denominator L'-1 make the FIRST and
//   the LAST emitted sample of EVERY grain exactly 0, for every
//   GrainEnvelopeType and every legal L' >= 2, unconditionally - including
//   Exponential, whose generated tail ends at ~0.0183 rather than 0
//   (grain_envelope.h:144-150).
//
//   FORCING TABLE ENTRIES TO 0 DOES NOT BOUND THE STEP AT THE NEIGHBOURING
//   SAMPLE, AND THAT IS WHAT A CLICK DETECTOR MEASURES. The per-sample
//   table-index step is Delta = (kEnvelopeTableSize - 1) / (L' - 1), and it
//   exceeds 1 whenever L' < kEnvelopeTableSize - i.e. for any REQUESTED grain
//   shorter than 4096/sampleRate (~85 ms at 48 kHz). At the kMinGrainSeconds =
//   0.05 end, L' = 2400 at 48 kHz gives Delta = 1.707: the render STEPS OVER
//   the forced entries, so the sample next to the boundary reads the generated
//   window unchanged. A run of forced ZEROES cannot help here however long it
//   is - lookup interpolates linearly across the run's leading edge, so a
//   rendered sample can still land on the last un-forced value and the very
//   next one on 0. MEASURED with a zero run alone, Exponential at
//   grainSeconds = 0.05 opened on a 1.658 % step and closed on a 1.334 % step
//   of one grain's amplitude, and SC-003's detector resolved it clearly: that
//   cell needed sigma 21.0 to reach a 0 false-positive floor where the five
//   naturally-closing envelopes needed 11.5.
//
//   THE FIX IS A RAMP, NOT A RUN. kEnvelopeEdgeFadeEntries entries at EACH end
//   of the table are multiplied by a linear ramp to 0, so the window value one
//   render step inside the boundary is bounded by v_edge * Delta / F rather
//   than by v_edge. F = 64 against Delta_max = 4095 / (0.05 * 44100 - 1) =
//   1.858 bounds Exponential's edge step at 0.0183 * 1.858 / 64 = 5.3e-4;
//   measured 1.34e-3 opening / 4.2e-4 closing at 48 kHz, and the cell's
//   required sigma fell from 21.0 to 12.5 (see SC-003's TU header for the full
//   measured grid). F costs 64/4096 = 1.56 % of the window at each end, where
//   every naturally-closing type is already below 1e-3.
//
//   The kEnvelopeTailZeroEntries run is RETAINED on top of the ramp for the
//   independent float-rounding reason documented at its declaration.
//   prepare()'s sampleRate > 1.0 floor is a defensive guard against a
//   non-finite control dt, NOT an operating point: below 44.1 kHz the first and
//   last samples are still exactly 0, only the edge-step bound weakens.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (allocation confined to prepare())
// - Principle III: Modern C++20 (RAII, constexpr, no raw owning pointers)
// - Principle IX: Layer 3 (depends on Layers 0-2 only)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>          // L0  detail::isNaN / isInf / flushDenormal
#include <krate/dsp/core/grain_envelope.h>    // L0  GrainEnvelopeType, generate, lookup
#include <krate/dsp/core/math_constants.h>    // L0  kHalfPi, kPi
#include <krate/dsp/core/pitch_utils.h>       // L0  semitonesToRatio
#include <krate/dsp/core/random.h>            // L0  Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/rolling_capture_buffer.h>  // L1  capture ring + readStereoLinear
#include <krate/dsp/primitives/smoother.h>    // L1  OnePoleSmoother, LinearRamp,
                                              //     calculateOnePolCoefficient, ITERUM_NOINLINE
#include <krate/dsp/primitives/spectral_buffer.h>             // L1
#include <krate/dsp/primitives/stft.h>        // L1  STFT, OverlapAdd, WindowType
#include <krate/dsp/processors/grain_scheduler.h>             // L2
#include <krate/dsp/processors/spectral_freeze_oscillator.h>  // L2

#include <algorithm>
#include <array>
#include <bit>      // std::bit_floor, std::bit_ceil, std::has_single_bit
#include <cassert>  // prepare()-time sanity only (debug builds); never on the audio path
#include <chrono>   // Phase 11.5 Step 0c: test-gated stage timers only
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// AtmosphereEngine
// =============================================================================

/// @brief Self-granulating atmosphere layer: rolling capture ring -> ultra-long
///        grains -> optional spectral blur -> optional pure-freeze drone.
///
/// @note In-place processing is NOT supported (documented precondition): the
///       engine reads all of the input for capture before writing any output.
class AtmosphereEngine {
public:
    // -------------------------------------------------------------------------
    // Capacities
    // -------------------------------------------------------------------------

    /// Grain pool capacity. PROVISIONAL and measurement-backed (FR-022): the
    /// SC-004 cost lever of last resort may reduce it, which is a specified
    /// capability trade (the documented `density * grainSeconds <= kMaxGrains`
    /// region shrinks with it), not a raised baseline.
    static constexpr std::size_t kMaxGrains = 64;

    /// Envelope lookup table size (FR-027), per GrainEnvelopeType. Allocated in
    /// prepare(), never resized afterwards.
    static constexpr std::size_t kEnvelopeTableSize = 4096;

    /// Number of GrainEnvelopeType values (core/grain_envelope.h:13-21), i.e.
    /// the number of conditioned windows prepare() generates. setGrainEnvelope()
    /// selects among them; see regenerateEnvelopeBank() for why the whole bank
    /// exists rather than one regenerated-on-demand table.
    static constexpr std::size_t kEnvelopeTypeCount = 6;
    static_assert(static_cast<std::size_t>(GrainEnvelopeType::Exponential) + 1 ==
                      kEnvelopeTypeCount,
                  "a GrainEnvelopeType was added: the envelope bank must cover every type, "
                  "or setGrainEnvelope() would index past it");

    /// Entries forced to 0 at the TAIL of the envelope table after every
    /// generate() (FR-027). Two, not one, for two independent reasons:
    ///   1. The last emitted sample has phase EXACTLY 1.0 (denominator L'-1), so
    ///      it lands on table[kEnvelopeTableSize-1] - but only in exact
    ///      arithmetic. `ageSamples * (1/(L'-1))` is one float rounding, which
    ///      can leave the final phase a hair UNDER 1.0, and the lookup then
    ///      interpolates between the last TWO entries (grain_envelope.h:175-181).
    ///      The second forced entry is what makes that sample 0 regardless.
    ///   2. Whenever the per-sample table-index step Delta = 4095/(L'-1) is at
    ///      most 1 - i.e. L' >= kEnvelopeTableSize - the run also zeroes the
    ///      SECOND-TO-LAST sample. Below that (a short requested grain: L' = 2400
    ///      at grainSeconds = 0.05, 48 kHz) it does not - which is precisely why
    ///      the run is NOT what bounds the boundary step. See
    ///      kEnvelopeEdgeFadeEntries and the endpoint conditioning note in the
    ///      file banner.
    /// Both matter for Exponential, whose generated tail ends at ~0.0183 rather
    /// than 0 (grain_envelope.h:144-150).
    static constexpr std::size_t kEnvelopeTailZeroEntries = 2;

    /// Entries at EACH END of the envelope table over which the generated window
    /// is ramped linearly to 0 (FR-027). THIS, not the zero run above, is what
    /// bounds the first difference at a grain boundary.
    ///
    /// A rendered grain steps through the table by Delta = 4095/(L'-1) entries
    /// per sample, so the window value one render step inside the boundary is
    /// v_edge * Delta / F under the ramp, against v_edge with a zero run of any
    /// length (lookup interpolates across a run's leading edge, so a rendered
    /// sample can land on the last un-forced value with the next one at 0).
    ///
    /// SIZING. Delta is largest at the shortest legal REQUESTED grain, since
    /// FR-025 only ever truncates to a LONGER-lived floor over the documented
    /// operating region: Delta_max = 4095 / (kMinGrainSeconds * 44100 - 1) =
    /// 4095/2204 = 1.858. F = 64 is ~34x that, bounding Exponential's edge step
    /// (v_edge = exp(-4) = 0.0183) at 5.3e-4 of one grain's amplitude, and costs
    /// 1.56 % of the window at each end - a region where every naturally-closing
    /// type is already below 1e-3, so their shapes are unchanged in any
    /// measurable sense. Measured effect on SC-003 in the file banner.
    static constexpr std::size_t kEnvelopeEdgeFadeEntries = 64;

    /// `g`. Used at BOTH ends of the birth window, for two different reasons:
    ///   - FR-025's young-side guard: no grain ever reads an age below this.
    ///   - FR-014's admission margin: a grain is admitted only once
    ///     getAvailableSamples() >= its birth read age + this. Once the ring is
    ///     full that is an OLD-side bound of C - g, so tryBirthGrain() subtracts
    ///     `g` from the old end of the window too - see the headroom there for
    ///     why the two rules are otherwise not jointly satisfiable.
    static constexpr std::size_t kMinAgeSamples = 64;

    /// The margin the interpolator itself needs: readStereoLinear reads the
    /// samples at ages floor(age) and floor(age)+1 and clamps at
    /// getAvailableSamples() - 2, which is why every FR-025 bound below is
    /// `C - 2` and not `C - 1`. FR-014's admission margin is `g` above, which is
    /// strictly larger, so this value never binds on the admission path.
    static constexpr std::size_t kInterpMarginSamples = 2;
    static_assert(kMinAgeSamples >= kInterpMarginSamples,
                  "FR-014's admission margin must cover what the interpolator needs, or a "
                  "grain admitted by FR-014 could still read past the end of the ring");

    // -------------------------------------------------------------------------
    // Control cadence
    // -------------------------------------------------------------------------

    /// Absolute control grid (FR-005). The VALUE is copied from
    /// `ContinuousBody::kControlChunkSamples` (systems/continuous_body.h:97) and
    /// `HarmonicCloud::kControlChunkSamples` (systems/harmonic_cloud.h:144);
    /// there is no header dependency in either direction.
    static constexpr std::size_t kControlChunkSamples = 64;

    /// Drift-lane OU step interval (brownian_drift.h:105, harmonic_cloud.h:148).
    static constexpr int kDriftControlInterval = 32;

    // -------------------------------------------------------------------------
    // Smoothing times (FR-009)
    // -------------------------------------------------------------------------

    static constexpr float kSilenceRampMs = 10.0f;     ///< FR-007 latch ramp
    static constexpr float kGainSmoothMs = 50.0f;      ///< FR-028, process() per sample
    static constexpr float kBlurSmoothMs = 50.0f;      ///< FR-009, advanceSamples(hopSize)
    static constexpr float kFreezeMixRampMs = 100.0f;  ///< FR-052, LinearRamp
    static constexpr float kLevelSmoothMs = 20.0f;     ///< FR-061, process() per sample

    // -------------------------------------------------------------------------
    // Drift lane coefficients, transcribed from processors/brownian_drift.h
    // -------------------------------------------------------------------------

    static constexpr float kDriftTauMin = 0.2f;           // :97
    static constexpr float kDriftTauMax = 30.0f;          // :99
    static constexpr float kDriftInternalStd = 0.5f;      // :101
    static constexpr float kDriftOutputSmoothMs = 150.0f; // :103
    static constexpr float kDriftWalkLimit = 4.0f;        // :226
    static constexpr float kDriftDenormalFloor = 1e-20f;  // :228

    // -------------------------------------------------------------------------
    // Ranges (FR-009)
    // -------------------------------------------------------------------------

    static constexpr float kMinGrainSeconds = 0.05f;
    static constexpr float kMaxGrainSeconds = 30.0f;
    static constexpr float kMinDensity = 0.1f;  ///< == grain_scheduler.h:47's own floor
    static constexpr float kMaxDensity = 20.0f;
    static constexpr float kMaxPositionSeconds = 30.0f;
    static constexpr float kMaxPitchSemitones = 24.0f;
    static constexpr float kPitchSpreadCents = 1200.0f;
    static constexpr float kMaxDriftRangeSemitones = 12.0f;

    /// Hard bound on a grain's total static pitch AND on its drift envelope
    /// endpoints, so r stays in [2^-3, 2^3] = [0.125, 8] for the grain's whole
    /// life and FR-025's rMin/rMax are always well defined (FR-031).
    static constexpr float kMaxAbsGrainSemitones = 36.0f;

    static constexpr float kMaxDecorrelationMs = 30.0f;
    static constexpr float kMaxLevel = 2.0f;
    static constexpr float kMinCaptureSeconds = 1.0f;
    static constexpr float kMaxCaptureSeconds = 30.0f;
    static constexpr std::size_t kMinBlurFftSize = 256;
    static constexpr std::size_t kMaxBlurFftSize = 4096;
    static constexpr std::size_t kMinFreezeFftSize = 256;   // == spectral_freeze_oscillator.h:642
    static constexpr std::size_t kMaxFreezeFftSize = 8192;  // == :643
    static constexpr std::size_t kMinMaxBlockSamples = 64;
    static constexpr std::size_t kMaxMaxBlockSamples = 8192;

    // -------------------------------------------------------------------------
    // Seed salts (FR-070). Disjoint by construction: kDriftSaltBase spans
    // [0x4000, 0x4000 + kMaxGrains) and no other salt lands there.
    // -------------------------------------------------------------------------

    static constexpr std::size_t kGrainSalt = 0x1000;
    static constexpr std::size_t kBlurSalt = 0x2000;
    static constexpr std::size_t kSchedulerSalt = 0x3000;
    static constexpr std::size_t kDriftSaltBase = 0x4000;

    static constexpr std::uint32_t kDefaultSeed = 1u;

    // -------------------------------------------------------------------------
    // Structural invariants. If any of these moves, the design's arithmetic no
    // longer holds - they are asserted, not documented.
    // -------------------------------------------------------------------------

    static_assert(kControlChunkSamples % static_cast<std::size_t>(kDriftControlInterval) == 0,
                  "a control chunk must be a whole number of OU steps");
    static_assert(kMinAgeSamples >= kControlChunkSamples,
                  "FR-025's guard band must cover a whole control chunk: r is held constant "
                  "within a chunk, so the youngest age is only re-checked at chunk boundaries");
    static_assert(kMaxGrains <= 255,
                  "the active-index list stores slot indices in std::uint8_t");
    static_assert(kMaxGrains <= 64,
                  "getActiveSlotMask() returns one bit per slot in a std::uint64_t "
                  "(FR-072); the SC-004 lever only ever REDUCES kMaxGrains, so "
                  "this constrains nothing the plan permits");
    static_assert(kEnvelopeTailZeroEntries >= 2 && kEnvelopeTailZeroEntries < kEnvelopeTableSize,
                  "FR-027's forced tail run must cover at least the last two entries");
    static_assert(kEnvelopeEdgeFadeEntries > kEnvelopeTailZeroEntries &&
                      2 * kEnvelopeEdgeFadeEntries < kEnvelopeTableSize,
                  "FR-027's edge ramp must extend past the forced zero run it subsumes, and the "
                  "two ends must not meet");
    static_assert(kDriftSaltBase > kSchedulerSalt + kMaxGrains, "salt ranges must not overlap");

    // -------------------------------------------------------------------------
    // Prepare-time configuration
    // -------------------------------------------------------------------------

    /// Prepare-time configuration (FR-009). Every field is validated in
    /// prepare(); nothing here is read again afterwards except through the
    /// SNAPPED members.
    struct PrepareConfig {
        float captureSeconds = 8.0f;         ///< [1, 30]; the memory table above applies
        bool blurEnabled = true;
        bool freezeEnabled = true;
        std::size_t blurFftSize = 1024;      ///< [256, 4096], snapped DOWN to a power of two
        std::size_t freezeFftSize = 2048;    ///< [256, 8192], snapped DOWN to a power of two
        std::size_t maxBlockSamples = 2048;  ///< [64, 8192]; sizes the blur output FIFO
    };

    // -------------------------------------------------------------------------
    // Construction. Copy is DELETED because members are non-copyable:
    // SpectralBuffer declares a move ctor and no copy (spectral_buffer.h:51-52)
    // and STFT / OverlapAdd delete copy outright (stft.h:41-44, :187-190).
    // Move is defaulted; every member is noexcept-movable.
    // -------------------------------------------------------------------------

    AtmosphereEngine() noexcept = default;
    ~AtmosphereEngine() noexcept = default;

    AtmosphereEngine(const AtmosphereEngine&) = delete;
    AtmosphereEngine& operator=(const AtmosphereEngine&) = delete;
    AtmosphereEngine(AtmosphereEngine&&) noexcept = default;
    AtmosphereEngine& operator=(AtmosphereEngine&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Allocate and configure. THE ONLY NON-REAL-TIME-SAFE METHOD.
    ///
    /// Calling it a second time is legal and fully reconfigures: capacities,
    /// FFT geometry and latency all move to the new configuration. Ends with
    /// reset(), so a freshly prepared engine is silent (the ring is empty) with
    /// every control value at its FR-009 default.
    ///
    /// @param sampleRate Sample rate in Hz. Floored at 1.0 defensively - see
    ///        the sample-rate conditioning note in the file banner.
    /// @param config Validated in place; see PrepareConfig.
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        // 1. Rate floor. BrownianDrift::prepare uses the same guard (:122): a
        //    zero or negative rate makes the control dt non-finite.
        sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0;

        // 2. Validate and store the config.
        captureSeconds_ = std::clamp(isFinite(config.captureSeconds) ? config.captureSeconds : 8.0f,
                                     kMinCaptureSeconds, kMaxCaptureSeconds);
        maxBlockSamples_ =
            std::clamp(config.maxBlockSamples, kMinMaxBlockSamples, kMaxMaxBlockSamples);
        blurFftSize_ = snapFftSize(config.blurFftSize, kMinBlurFftSize, kMaxBlurFftSize);
        freezeFftSize_ = snapFftSize(config.freezeFftSize, kMinFreezeFftSize, kMaxFreezeFftSize);
        blurHopSize_ = blurFftSize_ / 4;
        blurEnabled_ = config.blurEnabled;
        freezeEnabled_ = config.freezeEnabled;

        // 3. Capture ring.
        capture_.prepare(sampleRate_, captureSeconds_);
        captureCapacity_ = capture_.getCapacitySamples();

        // 4. Envelope BANK: every GrainEnvelopeType, generated ONCE here.
        //    setGrainEnvelope() is then a pointer swap - see kEnvelopeTypeCount.
        envelopeTable_.assign(kEnvelopeTableSize * kEnvelopeTypeCount, 0.0f);
        regenerateEnvelopeBank();

        // 5. Scheduler. density_/jitter_ are pushed into it by the control step,
        //    not from here - see setDensity()/setJitter().
        scheduler_.prepare(sampleRate_);

        // 5b. Phase 11.5 pass-A/pass-B scratch (renderGrainChunk). Sized ONCE
        //     here: <= kMaxGrains grains active at a chunk start plus
        //     <= kControlChunkSamples births can retire inside one chunk, and
        //     kControlChunkSamples == kMaxGrains, so 2 * kMaxGrains bounds both.
        retiredScratch_.assign(kMaxGrains * 2, RetiredGrainSpan{});
        dueScratch_.assign(kMaxGrains * 2, DueEntry{});

        // 6. Drift lanes.
        driftSmoothCoeff_ =
            calculateOnePolCoefficient(kDriftOutputSmoothMs, static_cast<float>(sampleRate_));
        updateDriftCoefficients();

        // 7. Blur. When disabled, every blur object is returned to its
        //    unprepared state and every vector released (FR-045) - a second
        //    prepare() with blurEnabled = false must not leave the previous
        //    geometry alive.
        if (blurEnabled_) {
            // 75 % OVERLAP IS THE GEOMETRY, NOT A TUNING CHOICE. applySynthesisWindow
            // is mandatory at >= 75 % overlap and FORBIDDEN at 50 % (stft.h:201-204),
            // so the hop and the synthesis-window flag below are one decision. The
            // assert is a debug-build tripwire against a later edit that changes the
            // hop divisor and leaves the flag alone; it never runs on the audio path.
            assert(blurHopSize_ * 4 == blurFftSize_ &&
                   "blur geometry must be 75 % overlap: hop == fftSize / 4");

            const std::size_t fifoCapacity =
                std::bit_ceil(blurFftSize_ + std::max(maxBlockSamples_, kControlChunkSamples) +
                              blurHopSize_);
            blurFifoMask_ = fifoCapacity - 1;
            for (std::size_t ch = 0; ch < 2; ++ch) {
                blurStft_[ch].prepare(blurFftSize_, blurHopSize_, WindowType::Hann);
                blurOla_[ch].prepare(blurFftSize_, blurHopSize_, WindowType::Hann, 9.0f,
                                     /*applySynthesisWindow=*/true);
                blurSpectrum_[ch].prepare(blurFftSize_);
                blurFifo_[ch].assign(fifoCapacity, 0.0f);
                fifoScratch_[ch].assign(blurHopSize_, 0.0f);
            }
        } else {
            blurFifoMask_ = 0;
            for (std::size_t ch = 0; ch < 2; ++ch) {
                blurStft_[ch] = STFT{};
                blurOla_[ch] = OverlapAdd{};
                blurSpectrum_[ch] = SpectralBuffer{};
                blurFifo_[ch] = std::vector<float>{};
                fifoScratch_[ch] = std::vector<float>{};
            }
        }

        // 8. Freeze. The capture scratch is sized from the OSCILLATOR'S OWN
        //    snapped size (spectral_freeze_oscillator.h:426-428), never from the
        //    requested config value: freeze() truncates at its fftSize_ (:222)
        //    and zero-pads a short block, so a mismatch silently discards the
        //    newest audio (FR-051).
        if (freezeEnabled_) {
            for (std::size_t ch = 0; ch < 2; ++ch) {
                freezeOsc_[ch].prepare(sampleRate_, freezeFftSize_);
                freezeCapture_[ch].assign(freezeOsc_[ch].getFftSize(), 0.0f);
            }
            if (blurEnabled_) {
                // The freeze leg is delay-matched to the blur latency (FR-052).
                freezeDelayMask_ = blurFftSize_ - 1;  // power of two after snapping
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    freezeDelay_[ch].assign(blurFftSize_, 0.0f);
                }
            } else {
                freezeDelayMask_ = 0;
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    freezeDelay_[ch] = std::vector<float>{};
                }
            }
        } else {
            freezeDelayMask_ = 0;
            for (std::size_t ch = 0; ch < 2; ++ch) {
                freezeOsc_[ch] = SpectralFreezeOscillator{};
                freezeCapture_[ch] = std::vector<float>{};
                freezeDelay_[ch] = std::vector<float>{};
            }
        }

        // 9. Smoothers, against the stored rate.
        const auto sr = static_cast<float>(sampleRate_);
        gainSmoother_.configure(kGainSmoothMs, sr);
        levelSmoother_.configure(kLevelSmoothMs, sr);
        blurSmoother_.configure(kBlurSmoothMs, sr);
        freezeMixRamp_.configure(kFreezeMixRampMs, sr);

        // 10. FR-007 ramp step.
        silenceStep_ = 1.0f / std::max(1.0f, kSilenceRampMs * 0.001f * sr);

        // 11.
        prepared_ = true;
        reset();
    }

    /// @brief Return to the exact post-prepare state. Allocation-free.
    ///
    /// This is also the ONE documented re-entry out of the FR-007 silence latch
    /// (there is no resume()), including after an internal non-finite trip.
    void reset() noexcept {
        // 1. Capture + clocks.
        capture_.reset();
        writeCounter_ = 0;
        sampleCounter_ = 0;

        // 2. Grains.
        grains_.fill(AtmosphereGrain{});
        activeIdx_.fill(std::uint8_t{0});
        activeCount_ = 0;
        nextSlot_ = 0;

        // 3. Scheduler. The seed() call is NOT optional: GrainScheduler::reset()
        //    and prepare() never touch rng_ (grain_scheduler.h:33-42), only
        //    seed() does (:97). Without it the jitter stream resumes mid-
        //    sequence and the post-reset render does not reproduce the original
        //    (FR-006).
        scheduler_.reset();
        scheduler_.seed(deriveStreamSeed(seed_, kSchedulerSalt));

        // 4. Engine-level streams.
        grainRng_.seed(deriveStreamSeed(seed_, kGrainSalt));
        blurRng_.seed(deriveStreamSeed(seed_, kBlurSalt));

        // 5. Drift lanes: zeroed AND re-seeded. Re-seeding on reset is
        //    BrownianDrift::reset()'s documented behaviour (:133-135 -> :243);
        //    a GRAIN BIRTH is the case that must not re-seed.
        resetDriftLanes();

        // 6. Blur, guarded on blurEnabled_ - when blur is disabled no FIFO was
        //    allocated and a non-zero occupancy would describe a buffer that
        //    does not exist.
        //
        //    blurFifoWrite_ must LEAD blurFifoRead_ by the occupancy; the ring
        //    invariant is
        //        blurFifoWrite_ == (blurFifoRead_ + blurFifoCount_) & blurFifoMask_
        //    Setting both cursors to 0 with a non-zero count leaves the reader
        //    permanently blurFftSize_ indices AHEAD of the writer: the real
        //    latency silently becomes the FIFO capacity rather than the
        //    blurFftSize_ that getLatencySamples() reports, and the failure
        //    presents as a COLA/windowing bug rather than a FIFO-init bug.
        if (blurEnabled_) {
            for (std::size_t ch = 0; ch < 2; ++ch) {
                blurStft_[ch].reset();
                blurOla_[ch].reset();
                blurSpectrum_[ch].reset();
                std::fill(blurFifo_[ch].begin(), blurFifo_[ch].end(), 0.0f);
                std::fill(fifoScratch_[ch].begin(), fifoScratch_[ch].end(), 0.0f);
            }
            blurFifoRead_ = 0;
            blurFifoWrite_ = blurFftSize_ & blurFifoMask_;
            blurFifoCount_ = blurFftSize_;
        } else {
            blurFifoRead_ = 0;
            blurFifoWrite_ = 0;
            blurFifoCount_ = 0;
        }
        blurSmoother_.snapTo(blur_);

        // 7. Freeze. SpectralFreezeOscillator::reset() is public, documented
        //    real-time safe and non-allocating (:173-196), and early-outs on
        //    !prepared_ (:174), so it is safe to call unconditionally.
        for (std::size_t ch = 0; ch < 2; ++ch) {
            freezeOsc_[ch].reset();
            std::fill(freezeDelay_[ch].begin(), freezeDelay_[ch].end(), 0.0f);
            std::fill(freezeCapture_[ch].begin(), freezeCapture_[ch].end(), 0.0f);
        }
        freezeDelayIdx_ = 0;

        // 8. Smoothers snapped to their current targets (FR-006).
        gainSmoother_.snapTo(1.0f);  // n = 0 => 1/sqrt(max(1,n)) = 1
        levelSmoother_.snapTo(level_);
        freezeMixRamp_.snapTo(freezeMix_);

        // 9. Output stage.
        silenceGain_ = 1.0f;
        runState_ = RunState::Running;
        chunkPoisoned_ = false;
        busPoisonAccum_ = 0.0f;
        busL_.fill(0.0f);
        busR_.fill(0.0f);
        wetL_.fill(0.0f);
        wetR_.fill(0.0f);
        freezeL_.fill(0.0f);
        freezeR_.fill(0.0f);

        // 10. Introspection counters. minObservedAge_ is seeded at the RING
        //     CAPACITY, never at an infinity sentinel - no <limits> infinity
        //     sentinel may appear anywhere in this component (the macOS leg
        //     builds -ffast-math, where such a sentinel folds to finite
        //     garbage). Any real observation is <= C - 2 < C, so the first
        //     observation always lowers it. Both age accessors are meaningless
        //     until getTotalGrainsBorn() > 0.
        skipPoolFull_ = 0;
        skipRingCold_ = 0;
        totalBorn_ = 0;
        totalRetired_ = 0;
        minObservedAge_ = static_cast<float>(captureCapacity_);
        maxObservedAge_ = 0.0f;
        lastBirthAge_ = 0.0f;
        lastBirthRatio_ = 1.0f;
        lastBirthLifetime_ = 0;
        lastBirthSlot_ = 0;
        lastBirthPanL_ = 1.0f;
        lastBirthPanR_ = 1.0f;
    }

    /// @brief Fade out over kSilenceRampMs and LATCH (FR-007).
    ///
    /// While Silencing the engine keeps processing normally and multiplies its
    /// output by a linearly decaying gain. When that gain reaches 0 every grain
    /// is retired and the engine latches: processStereoBlock() then writes
    /// exactly 0.0f and returns immediately - no capture, no scheduler tick, no
    /// grain ageing, no drift advance, no control-grid advance, no counter
    /// movement.
    ///
    /// Idempotent: a second call while Silencing or Latched does nothing (in
    /// particular it does not restart the ramp). reset() - or a fresh prepare()
    /// - is the ONLY re-entry. There is no resume().
    void silence() noexcept {
        if (runState_ == RunState::Latched) {
            return;
        }
        // The ramp continues from wherever silenceGain_ currently is.
        runState_ = RunState::Silencing;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Render one block. Shape identical to
    ///        ContinuousBody::processStereoBlock (systems/continuous_body.h:1161-1163)
    ///        so Phase 7 chains them without an adapter.
    ///
    /// Output is the WET TEXTURE ONLY (FR-062): the input stream appears in the
    /// output via grains and freeze, never as a dry pass-through.
    ///
    /// @pre outLeft/outRight must not alias inLeft/inRight (in-place is not
    ///      supported).
    void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft,
                            float* outRight, std::size_t numSamples) noexcept {
        // Guard order mirrors continuous_body.h:1166-1180 exactly.
        // 1. Any null pointer: write NOTHING and return (FR-004).
        if (inLeft == nullptr || inRight == nullptr || outLeft == nullptr || outRight == nullptr) {
            return;
        }
        // 2. Empty block: no-op, and the control grid does NOT advance (FR-004).
        if (numSamples == 0) {
            return;
        }
        // 3. Not prepared: zero-fill and return.
        if (!prepared_) {
            std::fill(outLeft, outLeft + numSamples, 0.0f);
            std::fill(outRight, outRight + numSamples, 0.0f);
            return;
        }
        // 4. Latched (FR-007): zero-fill and return, advancing nothing.
        if (runState_ == RunState::Latched) {
            std::fill(outLeft, outLeft + numSamples, 0.0f);
            std::fill(outRight, outRight + numSamples, 0.0f);
            return;
        }

        // FR-005's ABSOLUTE control grid. The grid is anchored to
        // sampleCounter_, NOT to block starts: a 64-sample chunk split 36 + 28
        // by a block boundary yields exactly the same control step as an
        // unsplit 64, which is what SC-011 measures. `n` is <= 64 always, so
        // every scratch array is a fixed 64-sample member and the blur pump
        // never pushes more than 64 samples between drains.
        std::size_t done = 0;
        while (done < numSamples) {
            const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
            const std::size_t toGrid = kControlChunkSamples - phase;
            const std::size_t n = std::min(numSamples - done, toGrid);

            // Phase 11.5 Step 0c. FALSE on every shipping path (see ProcessStage).
            const bool inst = processInstrumented_;
            std::chrono::steady_clock::time_point instT{};
            if (inst) {
                instT = std::chrono::steady_clock::now();
            }

            // (A) Control step - ONLY on an exact grid boundary.
            if (phase == 0) {
                runControlStep();
            }
            if (inst) {
                instT = stageLap(processStageNs_[static_cast<std::size_t>(ProcessStage::Control)],
                                 instT);
            }

            // (B) Capture, schedule, birth and per-sample grain accumulation
            //     into busL_ / busR_.
            renderGrainChunk(inLeft + done, inRight + done, n);
            if (inst) {
                instT = stageLap(processStageNs_[static_cast<std::size_t>(ProcessStage::Grains)],
                                 instT);
            }

            // (C) Blur. When enabled the grain sum is routed through the
            //     STFT <-> OverlapAdd stage UNCONDITIONALLY (FR-041), so the path
            //     is transparent at blur = 0 and the layer latency never moves
            //     with the knob (RA-3). The pump is control-chunk bounded: at
            //     most kControlChunkSamples samples are pushed between drains,
            //     which is what keeps STFT::samplesAvailable_ below 8*fftSize at
            //     every legal geometry (stft.h:104-113 has no overflow guard).
            //     When disabled the grain bus - already carrying the FR-028
            //     1/sqrt(n) population gain, applied per sample inside
            //     renderGrainChunk() - passes through unchanged (FR-045).
            if (blurEnabled_) {
                pumpBlur(n);
            } else {
                const auto span = static_cast<std::ptrdiff_t>(n);
                std::copy(busL_.begin(), busL_.begin() + span, wetL_.begin());
                std::copy(busR_.begin(), busR_.begin() + span, wetR_.begin());
            }
            if (inst) {
                instT = stageLap(processStageNs_[static_cast<std::size_t>(ProcessStage::Blur)],
                                 instT);
            }

            // (D) The pure-freeze leg (FR-050 ... FR-053), rendered into
            //     freezeL_ / freezeR_. The drone is NEVER routed through the
            //     STFT stage - the spectral hold stays pure - but when blur is
            //     enabled it passes through a prepare-allocated
            //     blurFftSize_-sample delay, so both crossfade legs share one
            //     layer latency and FR-046 reports a single honest number.
            renderFreezeChunk(n);
            if (inst) {
                instT = stageLap(processStageNs_[static_cast<std::size_t>(ProcessStage::Freeze)],
                                 instT);
            }

            // (E) The FR-052 crossfade, the FR-061 level trim, the FR-007
            //     silence ramp and the FR-064 denormal flush. The result is
            //     FR-062 compliant: the input reaches the output ONLY through
            //     grains and the freeze drone, never as a dry pass-through.
            finishChunk(outLeft + done, outRight + done, n);
            if (inst) {
                (void)stageLap(processStageNs_[static_cast<std::size_t>(ProcessStage::Finish)],
                               instT);
            }

            sampleCounter_ += n;
            done += n;

            // The FR-007 ramp can reach 0 partway through a block. From that
            // point NOTHING advances - not the control grid, not the capture,
            // not the scheduler - so the remainder of the block is zero-filled
            // here rather than rendered and then discarded. Without this, a
            // latch that lands at sample 100 of a 512-sample block would still
            // capture and tick the scheduler for the other 412 samples, and the
            // "no counter moves across the latched span" guarantee would hold
            // only for blocks that BEGAN latched.
            if (runState_ == RunState::Latched) {
                std::fill(outLeft + done, outLeft + numSamples, 0.0f);
                std::fill(outRight + done, outRight + numSamples, 0.0f);
                return;
            }
        }
    }

    // =========================================================================
    // Control table (FR-009)
    //
    // Every setter is noexcept, sanitises a NON-FINITE argument to the field's
    // DEFAULT, then clamps to the stated range. The sanitiser is the single
    // isFinite() composition below; no setter writes its own bit test.
    //
    // SNAPSHOT RULE (binding). setPitchSemitones, setPitchSpread and
    // setDriftRangeSemitones are read ONLY at grain birth. Everything derived
    // from them is frozen into the grain, so changing any of the three affects
    // only grains born AFTERWARDS. At grainSeconds = 30 they therefore take up
    // to a grain lifetime to fully take effect. This is what keeps FR-025
    // closed-form: no setter and no host automation can widen an in-flight
    // grain's ratio envelope past the one its lifetime was truncated for, so
    // there is no runtime age clamp anywhere in the grain read path.
    // =========================================================================

    /// Grain lifetime in seconds. Range [0.05, 30], default 4.0. Read at birth.
    void setGrainSeconds(float seconds) noexcept {
        grainSeconds_ = std::clamp(isFinite(seconds) ? seconds : 4.0f, kMinGrainSeconds,
                                   kMaxGrainSeconds);
    }
    [[nodiscard]] float getGrainSeconds() const noexcept { return grainSeconds_; }

    /// Trigger density in grains/second. Range [0.1, 20], default 4.0.
    /// The lower bound is the SAME bound GrainScheduler::setDensity enforces
    /// (grain_scheduler.h:47), so the control table and the component agree
    /// instead of the component silently overriding.
    /// @note Pushed into the scheduler at the next control step, never here: an
    ///       immediate push would move the scheduler's interonset mid-chunk and
    ///       make the render depend on where the block boundary fell.
    void setDensity(float grainsPerSecond) noexcept {
        density_ = std::clamp(isFinite(grainsPerSecond) ? grainsPerSecond : 4.0f, kMinDensity,
                              kMaxDensity);
    }
    [[nodiscard]] float getDensity() const noexcept { return density_; }

    /// Trigger-timing randomness. Range [0, 1], default 0.5.
    /// Pushed into the scheduler at the next control step, never here.
    void setJitter(float amount) noexcept {
        jitter_ = std::clamp(isFinite(amount) ? amount : 0.5f, 0.0f, 1.0f);
    }
    [[nodiscard]] float getJitter() const noexcept { return jitter_; }

    /// Birth read-age target, in seconds behind the write head.
    /// Range [0, 30], default 1.0.
    void setPositionSeconds(float seconds) noexcept {
        positionSeconds_ =
            std::clamp(isFinite(seconds) ? seconds : 1.0f, 0.0f, kMaxPositionSeconds);
    }
    [[nodiscard]] float getPositionSeconds() const noexcept { return positionSeconds_; }

    /// Per-grain randomisation of the read age, as a fraction of the position.
    /// Range [0, 1], default 0.3.
    void setPositionSpread(float spread) noexcept {
        positionSpread_ = std::clamp(isFinite(spread) ? spread : 0.3f, 0.0f, 1.0f);
    }
    [[nodiscard]] float getPositionSpread() const noexcept { return positionSpread_; }

    /// Static transposition in semitones. Range [-24, +24], default 0.
    /// SNAPSHOTTED at birth.
    void setPitchSemitones(float semitones) noexcept {
        pitchSemitones_ = std::clamp(isFinite(semitones) ? semitones : 0.0f, -kMaxPitchSemitones,
                                     kMaxPitchSemitones);
    }
    [[nodiscard]] float getPitchSemitones() const noexcept { return pitchSemitones_; }

    /// Per-grain pitch randomisation, 1.0 == +/-kPitchSpreadCents.
    /// Range [0, 1], default 0.15. SNAPSHOTTED at birth.
    void setPitchSpread(float spread) noexcept {
        pitchSpread_ = std::clamp(isFinite(spread) ? spread : 0.15f, 0.0f, 1.0f);
    }
    [[nodiscard]] float getPitchSpread() const noexcept { return pitchSpread_; }

    /// Drift-lane output depth. Range [0, 1], default 0.3. Live, whole bank.
    void setDriftDepth(float depth) noexcept {
        driftDepth_ = std::clamp(isFinite(depth) ? depth : 0.3f, 0.0f, 1.0f);
        driftLanes_.depth = driftDepth_;
    }
    [[nodiscard]] float getDriftDepth() const noexcept { return driftDepth_; }

    /// Drift-lane time constant, tau = lerp(kDriftTauMin, kDriftTauMax, x).
    /// Range [0, 1], default 0.7. Live, whole bank.
    void setDriftSmoothness(float smoothness) noexcept {
        driftSmoothness_ = std::clamp(isFinite(smoothness) ? smoothness : 0.7f, 0.0f, 1.0f);
        updateDriftCoefficients();
    }
    [[nodiscard]] float getDriftSmoothness() const noexcept { return driftSmoothness_; }

    /// Semitone range the lane value is scaled by. Range [0, 12], default 2.0.
    /// SNAPSHOTTED at birth: it scales that grain's lane value for its whole life.
    void setDriftRangeSemitones(float semitones) noexcept {
        driftRangeSemitones_ =
            std::clamp(isFinite(semitones) ? semitones : 2.0f, 0.0f, kMaxDriftRangeSemitones);
    }
    [[nodiscard]] float getDriftRangeSemitones() const noexcept { return driftRangeSemitones_; }

    /// Per-grain equal-power pan spread. Range [0, 1], default 0.7. Read at birth.
    void setPanSpread(float spread) noexcept {
        panSpread_ = std::clamp(isFinite(spread) ? spread : 0.7f, 0.0f, 1.0f);
    }
    [[nodiscard]] float getPanSpread() const noexcept { return panSpread_; }

    /// Per-grain L/R read-age decorrelation, 1.0 == kMaxDecorrelationMs.
    /// Range [0, 1], default 0.5. Read at birth.
    void setDecorrelation(float amount) noexcept {
        decorrelation_ = std::clamp(isFinite(amount) ? amount : 0.5f, 0.0f, 1.0f);
    }
    [[nodiscard]] float getDecorrelation() const noexcept { return decorrelation_; }

    /// Spectral blur (phase-randomisation) amount. Range [0, 1], default 0.
    /// Smoothed over kBlurSmoothMs and advanced by advanceSamples(hopSize)
    /// immediately BEFORE each blur frame reads it - never by process().
    void setBlur(float amount) noexcept {
        blur_ = std::clamp(isFinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
        blurSmoother_.setTarget(blur_);
    }
    [[nodiscard]] float getBlur() const noexcept { return blur_; }

    /// Crossfade towards the pure-freeze drone. Range [0, 1], default 0.
    /// 100 ms LinearRamp, process() per output sample.
    void setFreezeMix(float mix) noexcept {
        freezeMix_ = std::clamp(isFinite(mix) ? mix : 0.0f, 0.0f, 1.0f);
        freezeMixRamp_.setTarget(freezeMix_);
    }
    [[nodiscard]] float getFreezeMix() const noexcept { return freezeMix_; }

    /// @brief Arm the pure-freeze drone from the ring's newest window
    ///        (FR-050, FR-051).
    ///
    /// The extraction length is the OSCILLATOR'S OWN getFftSize()
    /// (processors/spectral_freeze_oscillator.h:426-428), never the requested
    /// PrepareConfig::freezeFftSize: freeze() truncates its input at fftSize_
    /// (:222-223), so a longer capture would silently discard the newest audio
    /// and a shorter one would zero-pad a window that is then read as a
    /// spectrum. Requesting an unsnapped size (e.g. 3000) is exactly how the
    /// two lengths come to disagree, which is why prepare() snaps first and
    /// this reads back from the oscillator.
    ///
    /// A capture attempted before the ring holds a WHOLE window is a no-op, not
    /// a partial capture: a zero-padded window is a different spectrum, not a
    /// quieter one.
    ///
    /// Allocation-free and lock-free. extractSlice() writes into the
    /// prepare-allocated freezeCapture_ scratch (both vectors are sized to
    /// getFftSize() in prepare()), and freeze() works entirely inside the
    /// oscillator's own pre-allocated buffers (:217-287) - its FFT included
    /// (primitives/fft.h:186-209 uses only prepare-owned scratch).
    void captureFreeze() noexcept {
        if (!prepared_ || !freezeEnabled_) {
            return;
        }
        const std::size_t need = freezeOsc_[0].getFftSize();
        if (need == 0 || capture_.getAvailableSamples() < need) {
            return;
        }
        capture_.extractSlice(freezeCapture_[0].data(), freezeCapture_[1].data(), need, 0);
        for (std::size_t ch = 0; ch < 2; ++ch) {
            freezeOsc_[ch].freeze(freezeCapture_[ch].data(), need);
        }
    }

    /// @brief Release the frozen drone. Each oscillator fades out over one hop
    ///        (processors/spectral_freeze_oscillator.h:295-300).
    ///
    /// Inert when freeze is disabled or nothing was captured (unfreeze() itself
    /// early-outs on !frozen_, :296).
    void releaseFreeze() noexcept {
        if (!prepared_ || !freezeEnabled_) {
            return;
        }
        for (auto& osc : freezeOsc_) {
            osc.unfreeze();
        }
    }

    /// True while the pure-freeze drone holds a captured spectrum. Exposed so a
    /// test that exercises captureFreeze() can prove the capture PATH ran
    /// rather than assuming it (the no-op branches above are silent).
    [[nodiscard]] bool isFreezeCaptured() const noexcept {
        return freezeEnabled_ && freezeOsc_[0].isFrozen();
    }

    /// Output gain trim - NOT a dry/wet mix and NOT a width control.
    /// Range [0, 2], default 1.0. 20 ms, process() per output sample.
    void setLevel(float level) noexcept {
        level_ = std::clamp(isFinite(level) ? level : 1.0f, 0.0f, kMaxLevel);
        levelSmoother_.setTarget(level_);
    }
    [[nodiscard]] float getLevel() const noexcept { return level_; }

    /// @brief Select the grain envelope shape. Default GrainEnvelopeType::Hann.
    ///
    /// SELECTS one of the kEnvelopeTypeCount windows prepare() already
    /// generated (regenerateEnvelopeBank()); it does NOT generate anything. The
    /// call is a store, so it is allocation-free, bounded and cheap enough to
    /// drive from an automation lane at block rate - which is exactly what
    /// SC-004 configuration (e) does.
    void setGrainEnvelope(GrainEnvelopeType type) noexcept {
        envelopeType_ = type;
    }
    [[nodiscard]] GrainEnvelopeType getGrainEnvelope() const noexcept { return envelopeType_; }

    /// @brief Re-seed every RNG stream (FR-070). Default seed is 1.
    ///
    /// Every derived value goes through deriveStreamSeed (core/random.h:102-111)
    /// with a distinct salt, so no two streams are correlated AND none can be
    /// handed 0 - Xorshift32::seed() silently substitutes its own default for 0
    /// (:73-75), so two streams hashing to 0 would collapse onto one.
    /// setSeed(0) is therefore a VALID, DISTINCT engine seed: the derivation,
    /// not the raw value, is what reaches Xorshift32::seed().
    ///
    /// Mid-render this re-seeds every lane's STREAM but zeroes no lane's walk
    /// state (only a grain birth does that), and it cannot touch a live grain's
    /// snapshotted pitch envelope or lifetime - so FR-025's invariant holds
    /// across a setSeed() at any point in a render.
    void setSeed(std::uint32_t seedValue) noexcept {
        seed_ = seedValue;
        grainRng_.seed(deriveStreamSeed(seedValue, kGrainSalt));
        blurRng_.seed(deriveStreamSeed(seedValue, kBlurSalt));
        scheduler_.seed(deriveStreamSeed(seedValue, kSchedulerSalt));
        for (std::size_t i = 0; i < kMaxGrains; ++i) {
            driftLanes_.rng[i].rng.seed(deriveStreamSeed(seedValue, kDriftSaltBase + i));
        }
    }
    [[nodiscard]] std::uint32_t getSeed() const noexcept { return seed_; }

    // =========================================================================
    // Phase 11.5 Step 0c - processStereoBlock() stage instrumentation
    // (test-only). Same discipline as SeraphisVoice::RenderStage: the gate is
    // FALSE on every shipping path, so a shipped render pays five always-false
    // branches per control chunk and never reads a clock.
    // =========================================================================
    enum class ProcessStage : std::size_t {
        Control,  ///< (A) runControlStep on the 64-sample grid
        Grains,   ///< (B) renderGrainChunk: capture, scheduler, grain sweep
        Blur,     ///< (C) pumpBlur STFT round-trip (or the copy at blur off)
        Freeze,   ///< (D) renderFreezeChunk: the two freeze oscillators + delay
        Finish,   ///< (E) finishChunk: crossfade, trims, silence ramp, flush
        Count
    };
    void setProcessInstrumentedForTest(bool on) noexcept { processInstrumented_ = on; }
    [[nodiscard]] double processStageNsForTest(ProcessStage stage) const noexcept {
        return processStageNs_[static_cast<std::size_t>(stage)];
    }

    // =========================================================================
    // Introspection (FR-072). All const, noexcept, allocation-free.
    // =========================================================================

    /// Number of grains currently alive.
    [[nodiscard]] std::size_t getActiveGrainCount() const noexcept { return activeCount_; }

    /// Triggers dropped because the pool was full (FR-023's skip-never-steal
    /// path). Kept SEPARATE from the cold-ring counter so the two causes are
    /// never conflated.
    [[nodiscard]] std::uint64_t getSkippedTriggerCountPoolFull() const noexcept {
        return skipPoolFull_;
    }

    /// Triggers dropped because the ring did not yet hold enough history
    /// (FR-014's path).
    [[nodiscard]] std::uint64_t getSkippedTriggerCountRingCold() const noexcept {
        return skipRingCold_;
    }

    /// Total grains born since reset().
    [[nodiscard]] std::uint64_t getTotalGrainsBorn() const noexcept { return totalBorn_; }

    /// Total grains retired since reset(), counted INDEPENDENTLY at every
    /// deactivation site rather than derived as (born - active). Derived, the
    /// identity `retired + active == born` is a tautology that a STEALING
    /// implementation also satisfies, so it could not detect FR-023 failing.
    [[nodiscard]] std::uint64_t getTotalGrainsRetired() const noexcept { return totalRetired_; }

    /// Bit `i` is set iff slot `i` holds a live grain. Exists because FR-020's
    /// round-robin slot coverage is otherwise unobservable. A const 64-iteration
    /// scan; it adds no state. (kMaxGrains <= 64 is static_asserted above.)
    [[nodiscard]] std::uint64_t getActiveSlotMask() const noexcept {
        std::uint64_t mask = 0;
        for (std::size_t i = 0; i < kMaxGrains; ++i) {
            if (grains_[i].active) {
                mask |= (std::uint64_t{1} << i);
            }
        }
        return mask;
    }

    /// Slot the most recent birth landed in. The birth SEQUENCE of slot indices
    /// is what proves round-robin allocation rather than first-free.
    [[nodiscard]] std::size_t getLastBornGrainSlot() const noexcept { return lastBirthSlot_; }

    /// Equal-power pan gains of the most recent birth, so FR-032's
    /// `gL^2 + gR^2 ~= 1` law can be asserted on real drawn values.
    void getLastBornGrainPanGains(float& outLeft, float& outRight) const noexcept {
        outLeft = lastBirthPanL_;
        outRight = lastBirthPanR_;
    }

    /// Smallest read age observed on any live grain since reset().
    /// @note Meaningless until getTotalGrainsBorn() > 0; seeded at the ring
    ///       capacity so the first real observation always lowers it.
    [[nodiscard]] float getMinObservedGrainAgeSamples() const noexcept { return minObservedAge_; }

    /// Largest read age observed on any live grain since reset().
    /// @note Meaningless until getTotalGrainsBorn() > 0.
    [[nodiscard]] float getMaxObservedGrainAgeSamples() const noexcept { return maxObservedAge_; }

    /// Birth read age of the most recent grain (drawn from internal RNG).
    [[nodiscard]] float getLastBornGrainBirthAgeSamples() const noexcept { return lastBirthAge_; }

    /// Playback ratio of the most recent grain at birth (drawn from internal RNG).
    [[nodiscard]] float getLastBornGrainRatioAtBirth() const noexcept { return lastBirthRatio_; }

    /// Truncated lifetime of the most recent grain, in samples.
    [[nodiscard]] std::uint64_t getLastBornGrainLifetimeSamples() const noexcept {
        return lastBirthLifetime_;
    }

    /// Raw state of the GRAIN stream (core/random.h:79). The only way to prove
    /// the blur stage did not consume from it (FR-044).
    [[nodiscard]] std::uint32_t getGrainRngState() const noexcept { return grainRng_.state(); }

    /// @brief Latency of the WHOLE layer, in samples - both crossfade legs.
    ///
    /// The freeze leg is delay-matched to the same figure, so there is never a
    /// second, different latency for a caller to discover. Constant between
    /// prepare() calls; it does NOT move with the blur knob. The freeze
    /// oscillator's own getLatencySamples() (spectral_freeze_oscillator.h:421-423)
    /// is deliberately not added: the drone is synthesised, not a delayed copy
    /// of the input, so it has no dry counterpart to align against.
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return blurEnabled_ ? blurFftSize_ : 0;
    }

    /// Capacity of the rolling capture ring, in samples. Rounded UP to a power
    /// of two by RollingCaptureBuffer, so the capacity in SECONDS is
    /// rate-dependent.
    [[nodiscard]] std::size_t getCaptureCapacitySamples() const noexcept {
        return captureCapacity_;
    }

    /// The SNAPPED freeze FFT size actually in force (0 when freeze is
    /// disabled). Exposed because FR-009's snap-before-store rule is otherwise
    /// unobservable: getLatencySamples() reports the BLUR geometry only.
    [[nodiscard]] std::size_t getFreezeFftSize() const noexcept {
        return freezeEnabled_ ? freezeFftSize_ : 0;
    }

    /// Current smoothed value of drift lane `slot`. Out-of-range returns 0.
    [[nodiscard]] float getDriftLaneValue(std::size_t slot) const noexcept {
        return (slot < kMaxGrains) ? driftLanes_.smoothCur[slot] : 0.0f;
    }

    /// @brief The blur amount the stage is CURRENTLY applying, i.e. the smoothed
    ///        value - not setBlur()'s target, which getBlur() returns.
    ///
    /// Exists because FR-009's smoother-cadence rule is otherwise unobservable.
    /// The smoother is advanced by advanceSamples(blurHopSize_) exactly ONCE per
    /// frame-pair, outside the per-channel loop (plan S11.2/S11.4); an advance
    /// left INSIDE that loop runs twice per hop of audio and halves the 50 ms
    /// time constant to ~25 ms. Every success criterion that sweeps blur sweeps
    /// SETTLED values, where the two are identical, so the settling-time clause
    /// in AtmosphereEngine_ControlTableClamps - reading this accessor - is the
    /// only thing in the phase that can see the difference.
    ///
    /// With blur disabled the smoother is never advanced, so this stays at
    /// whatever the last reset() snapped it to and setBlur() moves only the
    /// target that getBlur() reports.
    [[nodiscard]] float getAppliedBlur() const noexcept {
        return blurSmoother_.getCurrentValue();
    }

private:
    // =========================================================================
    // Nested state
    // =========================================================================

    /// One live grain. Deliberately NOT named `Grain`: `struct Grain` already
    /// exists at namespace scope (primitives/grain_pool.h:23).
    ///
    /// EVERYTHING PITCH-RELATED IS SNAPSHOTTED AT BIRTH (FR-009). No setter and
    /// no host automation can widen a live grain's ratio envelope past the one
    /// its lifetime was truncated for.
    struct AtmosphereGrain {
        std::uint64_t readIndexInt = 0;  ///< absolute source index, integer part
        float readFrac = 0.0f;           ///< absolute source index, fraction in [0,1)
        float ratio = 1.0f;              ///< r, recomputed per control step, held within a chunk
        float staticSemis = 0.0f;        ///< s, snapshot
        float driftSemis = 0.0f;         ///< d, snapshot
        float semisLo = 0.0f;            ///< clamp(s-d, +/-kMaxAbsGrainSemitones), snapshot
        float semisHi = 0.0f;            ///< clamp(s+d, +/-kMaxAbsGrainSemitones), snapshot
        float ratioMin = 1.0f;           ///< semitonesToRatio(semisLo), snapshot
        float ratioMax = 1.0f;           ///< semitonesToRatio(semisHi), snapshot
        float decorrAge = 0.0f;          ///< right-channel extra read age in samples
        float panL = 1.0f;               ///< equal-power gains, computed once at birth
        float panR = 1.0f;
        /// 1 / (L' - 1) - MULTIPLIED, never accumulated. The denominator is
        /// L'-1, NOT L', so the last emitted sample has phase EXACTLY 1.0 and
        /// lands on the forced table tail.
        float envPhaseInc = 0.0f;
        std::uint32_t lifetime = 0;    ///< L' in samples (FR-025 truncation), always >= 2
        std::uint32_t ageSamples = 0;  ///< samples since birth; retirement is an INTEGER compare
        bool active = false;
    };

    /// Wrapper so `std::array<..., kMaxGrains>{}` is value-initialisable:
    /// Xorshift32's only constructor is explicit (core/random.h:45). Named
    /// distinctly from HarmonicCloud::LaneRng (systems/harmonic_cloud.h:1121)
    /// and EntropyProcessor::LaneRng (processors/entropy_processor.h:423) so a
    /// reader is never in doubt which one is in scope. This holds the REAL
    /// Layer 0 RNG - never a hand-rolled xorshift, which would silently
    /// desynchronise from BrownianDrift's streams.
    struct DriftLaneRng {
        Xorshift32 rng{1};
    };

    /// kMaxGrains independent Ornstein-Uhlenbeck walks, SoA. Laid out exactly
    /// like HarmonicCloud::DriftLanes (systems/harmonic_cloud.h:1125-1148).
    /// NEVER kMaxGrains BrownianDrift objects.
    ///
    /// alignas(32) is for LOCALITY ONLY - nothing here is loaded with an aligned
    /// SIMD op and there is no Highway code in this component at all.
    struct GrainDriftLanes {
        alignas(32) std::array<float, kMaxGrains> walk{};       ///< x_i
        alignas(32) std::array<float, kMaxGrains> smoothCur{};  ///< 150 ms one-pole current
        alignas(32) std::array<float, kMaxGrains> smoothTgt{};  ///< 150 ms one-pole target
        std::array<DriftLaneRng, kMaxGrains> rng{};
        float a = 0.0f;               ///< AR(1) retention coefficient
        float g = 0.0f;               ///< AR(1) innovation gain
        float depth = 1.0f;           ///< BrownianDrift::setDepth semantics
        int samplesUntilControl = 0;  ///< SHARED across the whole bank
        int cachedPowN = 0;           ///< memo of std::pow(coeff, (float)N)
        float cachedPowValue = 0.0f;
    };

    enum class RunState : std::uint8_t { Running, Silencing, Latched };

    // =========================================================================
    // Helpers
    // =========================================================================

    /// FR-008: the ONE finiteness test in this component. A COMPOSITION of the
    /// existing Layer 0 helpers, never a new bit test - detail::isNaN
    /// (core/db_utils.h:54-57) and detail::isInf (:175-178) are already what
    /// OnePoleSmoother::setTarget (primitives/smoother.h:170-181) relies on.
    /// ContinuousBody already carries a private reimplementation
    /// (systems/continuous_body.h:1346-1358); this must not become a fifth.
    ///
    /// ITERUM_NOINLINE IS LOAD-BEARING, NOT STYLE. This is a header, and it
    /// lands in translation units built with /fp:fast (MSVC) and -ffast-math
    /// (the macOS leg, via the VST3 SDK's global flags). core/db_utils.h:44-52
    /// states the contract verbatim: "Source files using this function MUST be
    /// compiled with -fno-fast-math." A header cannot impose that on its
    /// consumers, so the repo's established remedy is to put the check behind a
    /// call boundary the caller's fast-math context cannot see through -
    /// primitives/smoother.h:37-45 defines ITERUM_NOINLINE with the comment
    /// "Required to prevent branch elimination with NaN checks under /fp:fast".
    /// `constexpr` is DELIBERATELY ABSENT: it is an inlining invitation, i.e.
    /// exactly the opposite of what is wanted here.
    ///
    /// The call boundary is not free (~2 ns), so the hot paths call this O(1)
    /// times per 64-sample control chunk (accumulate-then-test), never once per
    /// sample. Control-rate callers - every setter above - call it directly;
    /// one call per setter per block is immaterial.
    /// @note The attribute ORDER matters: `[[nodiscard]]` must precede
    ///       ITERUM_NOINLINE. GCC and Clang reject a standard
    ///       attribute-specifier-seq that follows a GNU `__attribute__` here
    ///       ("an attribute list cannot appear here"), while MSVC accepts both
    ///       orders - so the wrong order builds clean on Windows and breaks the
    ///       Linux and macOS legs (dsp/CLAUDE.md, "MSVC accepts what GCC and
    ///       Clang reject").
    [[nodiscard]] ITERUM_NOINLINE static bool isFinite(float v) noexcept {
        return !(detail::isNaN(v) || detail::isInf(v));
    }

    /// FR-009's FFT-size validation, in the binding order: clamp to the bounds
    /// FIRST, then bit_floor a non-power-of-two, then re-clamp to the lower
    /// bound. This is the order SpectralFreezeOscillator::prepare already uses
    /// internally (:107-113); doing it here as well is what stops the engine
    /// from KEEPING THE UNSNAPPED REQUEST - a request of 3000 must not leave a
    /// capture length of 3000 disagreeing with an analysis length of 2048.
    [[nodiscard]] static std::size_t snapFftSize(std::size_t requested, std::size_t minSize,
                                                 std::size_t maxSize) noexcept {
        std::size_t n = std::clamp(requested, minSize, maxSize);
        if (!std::has_single_bit(n)) {
            n = std::bit_floor(n);
        }
        return std::max(n, minSize);
    }

    /// FR-027. Regenerated IN PLACE; never resizes (contrast
    /// GrainProcessor::prepare, processors/grain_processor.h:49). Five of the
    /// six shipped types already end at exactly 0, but Exponential's release
    /// branch is exp(-t*4) (grain_envelope.h:144-150) and ends at ~0.0183.
    ///
    /// FORCING TABLE ENTRIES IS ONLY HALF THE FIX, IN TWO SEPARATE WAYS.
    ///   (a) GrainEnvelope::lookup maps phase to phase*(tableSize-1) (:175), so
    ///       under a 1/L' phase denominator the maximum phase is (L'-1)/L' < 1
    ///       and table[kEnvelopeTableSize-1] is NEVER READ - forcing it would
    ///       change nothing at all. The denominator is therefore L'-1 (see
    ///       AtmosphereGrain::envPhaseInc), which puts the last emitted sample
    ///       exactly on the forced tail.
    ///   (b) A forced value bounds the sample AT the boundary; what a click
    ///       detector measures is the STEP to its NEIGHBOUR, one table stride
    ///       Delta = 4095/(L'-1) away. At a short requested grain Delta > 1 and
    ///       the neighbour reads the generated window unchanged - so the edges
    ///       are RAMPED over kEnvelopeEdgeFadeEntries, which is what actually
    ///       bounds the step. See that constant and the file banner.
    /// The two mechanisms are independent and both are kept: the ramp bounds
    /// the neighbour, the zero run pins the boundary sample itself against float
    /// rounding in the phase product.
    ///
    /// GENERATED AT prepare() FOR EVERY TYPE, NOT ON DEMAND (SC-004). One
    /// GrainEnvelope::generate() over 4096 entries costs one or two
    /// transcendentals per entry; SC-004 configuration (e) calls
    /// setGrainEnvelope() once per 512-sample block with an ALTERNATING type, so
    /// an on-demand regeneration lands that whole cost inside the audio block -
    /// MEASURED at 87,415 ns/block on the reference machine against a 106,667 ns
    /// total budget, i.e. the regeneration alone was 82 % of the per-voice
    /// allowance. Generating all kEnvelopeTypeCount windows once, in prepare,
    /// costs 6 x 4096 x 4 B = 98 KiB per voice - against RA-2's 4.19 MB ring at
    /// the default captureSeconds that is 2.3 % more memory - and reduces
    /// setGrainEnvelope() to storing an enum. Every table is bit-identical to
    /// what the on-demand path produced, including FR-027's endpoint
    /// conditioning, which is applied to each bank entry below.
    void regenerateEnvelopeBank() noexcept {
        if (envelopeTable_.size() != kEnvelopeTableSize * kEnvelopeTypeCount) {
            return;
        }
        for (std::size_t t = 0; t < kEnvelopeTypeCount; ++t) {
            conditionEnvelope(envelopeTable_.data() + (t * kEnvelopeTableSize),
                              static_cast<GrainEnvelopeType>(t));
        }
    }

    /// @brief Generate ONE window into `table` and apply FR-027's endpoint
    ///        conditioning to it.
    static void conditionEnvelope(float* table, GrainEnvelopeType type) noexcept {
        GrainEnvelope::generate(table, kEnvelopeTableSize, type);

        // Edge ramps FIRST: they are a multiply on the generated window, so the
        // forced zeroes below must not be their input.
        constexpr auto kFade = static_cast<float>(kEnvelopeEdgeFadeEntries);
        for (std::size_t k = 0; k < kEnvelopeEdgeFadeEntries; ++k) {
            const float weight = static_cast<float>(k) / kFade;
            table[k] *= weight;
            table[kEnvelopeTableSize - 1 - k] *= weight;
        }

        table[0] = 0.0f;
        for (std::size_t k = kEnvelopeTableSize - kEnvelopeTailZeroEntries;
             k < kEnvelopeTableSize; ++k) {
            table[k] = 0.0f;
        }
    }

    /// @brief The bank entry the current GrainEnvelopeType selects, or nullptr
    ///        before prepare().
    [[nodiscard]] const float* activeEnvelope() const noexcept {
        if (envelopeTable_.size() != kEnvelopeTableSize * kEnvelopeTypeCount) {
            return nullptr;
        }
        return envelopeTable_.data() +
               (static_cast<std::size_t>(envelopeType_) * kEnvelopeTableSize);
    }

    /// Transcribed from BrownianDrift::updateCoefficients (:230-240), INCLUDING
    /// its double-precision intermediates. Computing tau/a/g in float instead
    /// moves the coefficients in the last bits, and the walk is an AR(1)
    /// recursion, so a coefficient difference is re-applied at every step.
    void updateDriftCoefficients() noexcept {
        const double controlDt = static_cast<double>(kDriftControlInterval) / sampleRate_;
        const double tau = static_cast<double>(kDriftTauMin) +
                           static_cast<double>(driftSmoothness_) *
                               (static_cast<double>(kDriftTauMax) -
                                static_cast<double>(kDriftTauMin));
        const double a = std::exp(-controlDt / tau);
        const double g =
            static_cast<double>(kDriftInternalStd) * std::sqrt(std::max(0.0, 1.0 - a * a));
        driftLanes_.a = static_cast<float>(a);
        driftLanes_.g = static_cast<float>(g);
    }

    /// Zero every lane's walk and smoother state, clear the pow memo, AND
    /// re-seed every lane. Re-seeding on reset is BrownianDrift::reset()'s
    /// documented behaviour (:133-135 -> :243); a GRAIN BIRTH is the case that
    /// must NOT re-seed - modelling a birth as a lane reset would hand every
    /// grain on a slot one identical walk.
    void resetDriftLanes() noexcept {
        driftLanes_.walk.fill(0.0f);
        driftLanes_.smoothCur.fill(0.0f);
        driftLanes_.smoothTgt.fill(0.0f);
        driftLanes_.samplesUntilControl = 0;
        driftLanes_.cachedPowN = 0;
        driftLanes_.cachedPowValue = 0.0f;
        driftLanes_.depth = driftDepth_;
        for (std::size_t i = 0; i < kMaxGrains; ++i) {
            driftLanes_.rng[i].rng.seed(deriveStreamSeed(seed_, kDriftSaltBase + i));
        }
    }

    /// @brief One OU control step for EVERY lane. Transcribed from
    ///        BrownianDrift::advanceControlStep (processors/brownian_drift.h:253-270).
    ///
    /// The three nextFloat() draws are SEQUENCED into named locals: the operands
    /// of `+` are unsequenced in C++, so summing three calls inline would leave
    /// the draw order unspecified - and a lane whose draw order differs from
    /// BrownianDrift's is a DIFFERENT STREAM, not a rounding difference.
    ///
    /// EVERY lane steps, live or not. Three binding reasons: it makes the bank's
    /// state after N advanced samples a function of N alone (SC-011); it makes
    /// SC-002's equivalence gate expressible against a plain BrownianDrift
    /// driven with the same chunk schedule; and a lane whose stream position
    /// depended on its slot's occupancy would make one grain's pitch a function
    /// of unrelated grains' lifetimes.
    void advanceControlStepAllLanes() noexcept {
        for (std::size_t i = 0; i < kMaxGrains; ++i) {
            const float z0 = driftLanes_.rng[i].rng.nextFloat();
            const float z1 = driftLanes_.rng[i].rng.nextFloat();
            const float z2 = driftLanes_.rng[i].rng.nextFloat();
            const float z = z0 + z1 + z2;  // Irwin-Hall: zero-mean, unit-variance

            // The mean is 0, so BrownianDrift's `mean + a*(x - mean)` collapses
            // to `a*x` (brownian_drift.h:262).
            float x = (driftLanes_.a * driftLanes_.walk[i]) + (driftLanes_.g * z);
            x = std::clamp(x, -kDriftWalkLimit, kDriftWalkLimit);
            if (x < kDriftDenormalFloor && x > -kDriftDenormalFloor) {
                x = 0.0f;
            }
            driftLanes_.walk[i] = x;

            // BrownianDrift::outputTarget() (:249-251) fed to
            // OnePoleSmoother::setTarget, which for a finite argument is a plain
            // assignment (primitives/smoother.h:170-181) - the clamp above
            // already guarantees finite.
            driftLanes_.smoothTgt[i] = std::clamp(driftLanes_.depth * x, -1.0f, 1.0f);
        }
    }

    /// @brief Advance every lane's 150 ms output one-pole by `numSamples`.
    ///
    /// THIS IS A TRANSCRIPTION OF OnePoleSmoother::advanceSamples
    /// (primitives/smoother.h:243-254), NOT the exponential identity. The naive
    /// `cur = tgt + (cur - tgt) * coeff^k` omits three operations the real
    /// smoother performs, all three observable:
    ///   1. the isComplete() early RETURN, which leaves current_ UNCHANGED - it
    ///      does not snap (:244, :232-234);
    ///   2. detail::flushDenormal (:250);
    ///   3. a post-advance HARD SNAP to target below kCompletionThreshold
    ///      (:251-253).
    /// On HarmonicCloud's equivalent gate the naive form measured up to 1.64e-4
    /// of divergence and this one measured 0.000e+00 (harmonic_cloud.h:1919-1924).
    ///
    /// coeff^N is formed by the SAME expression advanceSamples uses -
    /// std::pow(coefficient_, static_cast<float>(numSamples)) (:248) - and NEVER
    /// from a precomputed coeff^k table. A `for (k) table[k] = pow(coeff,
    /// float(k))` loop is unrolled by /O2, which makes every exponent a
    /// compile-time constant, and under /fp:fast (this repo's MSVC setting, and
    /// -ffast-math on the macOS leg) the compiler strength-reduces the
    /// constant-exponent pow into repeated multiplication: measured on
    /// HarmonicCloud, 4 ULP at N = 32, which the 150 ms pole's 1440-fold
    /// accumulation and the snap turn into 1.02e-4 of divergence
    /// (harmonic_cloud.h:1929-1938). The memo below is NOT that table - it is
    /// filled by this same call site with the same RUNTIME operand, so no
    /// exponent ever becomes a compile-time constant and the float served is
    /// bit-for-bit the float the uncached form computed. resetDriftLanes()
    /// clears it because driftSmoothCoeff_ only moves in prepare(), which calls
    /// reset(). The call is hoisted OUT of the lane loop: 1 powf per bank per
    /// chunk-step.
    ///
    /// @param numSamples Samples to advance; the caller only ever advances to
    ///        the next control boundary, so this is kDriftControlInterval on
    ///        every call and the memo serves one value.
    void advanceSmootherAllLanes(int numSamples) noexcept {
        if (numSamples <= 0) {  // advanceSamples(0) is a no-op (smoother.h:244)
            return;
        }
        if (driftLanes_.cachedPowN != numSamples) {
            driftLanes_.cachedPowValue =
                std::pow(driftSmoothCoeff_, static_cast<float>(numSamples));  // smoother.h:248
            driftLanes_.cachedPowN = numSamples;
        }
        const float coeffN = driftLanes_.cachedPowValue;
        for (std::size_t i = 0; i < kMaxGrains; ++i) {
            const float diff0 = driftLanes_.smoothCur[i] - driftLanes_.smoothTgt[i];
            if (std::abs(diff0) < kCompletionThreshold) {
                continue;  // smoother.h:244 - SKIP this lane, do NOT snap it
            }
            driftLanes_.smoothCur[i] = driftLanes_.smoothTgt[i] + (diff0 * coeffN);  // :247-249
            driftLanes_.smoothCur[i] = detail::flushDenormal(driftLanes_.smoothCur[i]);  // :250
            if (std::abs(driftLanes_.smoothCur[i] - driftLanes_.smoothTgt[i]) <
                kCompletionThreshold) {
                driftLanes_.smoothCur[i] = driftLanes_.smoothTgt[i];  // :251-253
            }
        }
    }

    /// @brief Advance the whole bank by `numSamples`, structurally mirroring
    ///        BrownianDrift::processBlock (processors/brownian_drift.h:194-206).
    ///
    /// samplesUntilControl is SHARED across the bank's lanes - every lane is
    /// advanced by the same sample counts, so one counter is both sufficient and
    /// correct, and it is what makes the bank's state a function of the TOTAL
    /// advanced samples rather than of how they were partitioned (SC-011).
    ///
    /// THE CALLER IS runControlStep(), WITH EXACTLY kControlChunkSamples - never
    /// "once per block" by numSamples. processBlock advances the output smoother
    /// for the whole span before returning, so a value read after a 4096-sample
    /// advance is 4096 samples further along the walk than the same value read
    /// under 64-sample partitions; since that value scales a grain's pitch, the
    /// two renders would diverge by orders of magnitude above SC-011's bound.
    ///
    /// A 64-sample control chunk therefore performs TWO internal OU steps,
    /// because kDriftControlInterval = 32.
    void advanceDriftLanes(std::size_t numSamples) noexcept {
        auto remaining = static_cast<int>(numSamples);
        while (remaining > 0) {
            if (driftLanes_.samplesUntilControl <= 0) {
                driftLanes_.samplesUntilControl = kDriftControlInterval;
                advanceControlStepAllLanes();
            }
            const int advance = std::min(remaining, driftLanes_.samplesUntilControl);
            driftLanes_.samplesUntilControl -= advance;
            remaining -= advance;
            advanceSmootherAllLanes(advance);
        }
    }

    // =========================================================================
    // Grain engine (plan S9.1 - S9.5, S9.7, S9.8)
    // =========================================================================

    /// @brief The single point at which a semitone offset becomes a playback
    ///        ratio. FR-024 names semitonesToRatio (core/pitch_utils.h:23).
    ///
    /// BOTH the birth-time envelope (ratioMin / ratioMax) and the per-control-
    /// step ratio go through HERE, so the monotone consistency between them is
    /// structural rather than a convention a later edit can break.
    [[nodiscard]] static float ratioAtPitch(float semitones) noexcept {
        return semitonesToRatio(semitones);
    }

    /// @brief Fold one observed read age into the FR-072 extremes.
    ///
    /// Called once per control chunk per live grain, from the chunk's FIRST and
    /// LAST sample and for BOTH channels, plus once at birth and once at
    /// retirement. That is EXACT, not a sample: `ratio` is held constant within
    /// a chunk (refreshed only by runControlStep()), so age(t) is affine in t
    /// over the chunk and its extremes are the two endpoints. Chunk-rate folding
    /// therefore loses nothing and costs a handful of compares per grain per 64
    /// samples instead of per sample.
    void foldObservedAge(float age) noexcept {
        if (age < minObservedAge_) {
            minObservedAge_ = age;
        }
        if (age > maxObservedAge_) {
            maxObservedAge_ = age;
        }
    }

    /// @brief Recompute one live grain's playback ratio from its drift lane
    ///        (FR-024, FR-030, plan S9.5).
    ///
    /// THE CLAMP TO THE SNAPSHOTTED ENDPOINTS IS LOAD-BEARING, not
    /// belt-and-braces. The lane value is clamped to [-1, +1] (as
    /// BrownianDrift::getCurrentValue() is, processors/brownian_drift.h:212-214),
    /// so lane * d is mathematically within +/-d - but the float add
    /// s + lane * d can round 1 ULP ABOVE s + d. Clamping the PITCH to the two
    /// floats the envelope was built from makes r land in [ratioMin, ratioMax]
    /// EXACTLY, because ratioAtPitch is monotone and deterministic. Without it a
    /// 1-ULP overshoot accumulated over 1.44 M samples is ~0.14 samples of extra
    /// age - enough to fail SC-002's `max <= C - 2` whenever the floor division
    /// in the truncation rule leaves no slack.
    void refreshGrainRatio(AtmosphereGrain& grain, std::size_t slot) noexcept {
        const float pitch =
            std::clamp(grain.staticSemis + (driftLanes_.smoothCur[slot] * grain.driftSemis),
                       grain.semisLo, grain.semisHi);
        grain.ratio = ratioAtPitch(pitch);
    }

    /// @brief Service one scheduler trigger (plan S9.2 - S9.4).
    ///
    /// TWO REJECTION CAUSES, TWO COUNTERS, NEVER CONFLATED:
    ///   - pool full  - a full round-robin sweep found no inactive slot. Costs
    ///                  NO grain-RNG draw, which is what keeps the grain stream
    ///                  a function of SUCCESSFUL births and therefore makes
    ///                  getGrainRngState() a usable determinism probe.
    ///   - ring cold  - the ring does not yet hold enough history, or the
    ///                  FR-025 headroom is exhausted. Evaluated AFTER the four
    ///                  draws, because its threshold depends on the drawn a0 and
    ///                  dR - so a ring-cold skip DOES consume four draws. That
    ///                  asymmetry is deliberate and is pinned here.
    ///
    /// FR-023 SKIP, NEVER STEAL: no in-flight grain is ever reset, truncated or
    /// reused. Stealing a 30 s grain mid-envelope is a guaranteed click
    /// (contrast GrainPool::acquireGrain, primitives/grain_pool.h:71-91).
    void tryBirthGrain() noexcept {
        // --- Slot sweep FIRST: kMaxGrains integer tests, no RNG (FR-020).
        //     Round-robin, not first-free: lane i is bound to slot i, so
        //     first-free would concentrate every grain on the low
        //     `density * grainSeconds` slots and reuse the same drift lanes over
        //     and over while the upper lanes idle, serially correlating
        //     successive grains in pitch motion.
        std::size_t slot = kMaxGrains;
        for (std::size_t k = 0; k < kMaxGrains; ++k) {
            const std::size_t candidate = (nextSlot_ + k) % kMaxGrains;
            if (!grains_[candidate].active) {
                slot = candidate;
                break;
            }
        }
        if (slot == kMaxGrains) {
            ++skipPoolFull_;
            return;
        }

        // --- The four birth draws, in this FIXED order. reset() and the seed-
        //     determinism gate must reproduce every draw; inserting, removing or
        //     reordering one re-shuffles every subsequent grain's parameters.
        const float uPos = grainRng_.nextFloat();     // [-1, 1] position spread
        const float uPitch = grainRng_.nextFloat();   // [-1, 1] static detune
        const float uPan = grainRng_.nextFloat();     // [-1, 1] pan
        const float uDec = grainRng_.nextUnipolar();  // [ 0, 1] decorrelation

        // --- (a) Pitch envelope snapshot (FR-031). The +/-36 clamp is applied
        //     to the ENVELOPE ENDPOINTS, not only to s, so r stays in
        //     [0.125, 8] at every instant of the grain's life and ratioMin /
        //     ratioMax are well defined and fixed for that life.
        const float staticSemis =
            std::clamp(pitchSemitones_ + (uPitch * pitchSpread_ * (kPitchSpreadCents / 100.0f)),
                       -kMaxAbsGrainSemitones, kMaxAbsGrainSemitones);
        const float driftSemis = driftRangeSemitones_;
        const float semisLo =
            std::clamp(staticSemis - driftSemis, -kMaxAbsGrainSemitones, kMaxAbsGrainSemitones);
        const float semisHi =
            std::clamp(staticSemis + driftSemis, -kMaxAbsGrainSemitones, kMaxAbsGrainSemitones);
        const float ratioMin = ratioAtPitch(semisLo);
        const float ratioMax = ratioAtPitch(semisHi);

        // --- (b) Decorrelation offset (FR-033). The RIGHT channel reads at
        //     ageL + decorrAge, i.e. L and R read DIFFERENT points of the ring.
        //     That is a genuine decorrelator, unlike stereoCrossBlend, which
        //     cannot decorrelate two correlated inputs.
        const float decorrAge = decorrelation_ * kMaxDecorrelationMs * 0.001f *
                                static_cast<float>(sampleRate_) * uDec;

        // --- (c) The FR-025 liveness arithmetic, in double so the two ceilings
        //     and the floor division are exact at every legal capacity.
        const double capacity = static_cast<double>(captureCapacity_);
        const double guard = static_cast<double>(kMinAgeSamples);
        const double decorr = static_cast<double>(decorrAge);
        // wUp: the age SHRINKS at this rate (the grain reads forward faster than
        // the write head). wDown: the age GROWS at this rate.
        const double wUp = std::max(static_cast<double>(ratioMax) - 1.0, 0.0);
        const double wDown = std::max(1.0 - static_cast<double>(ratioMin), 0.0);
        // THE SUM, NEVER THE MAXIMUM. When the envelope straddles r = 1 both
        // terms are non-zero: at s = 0, d = 2 the sum is 0.2316 against a
        // maximum of 0.1225. A maximum-based w under-truncates by ~2x and leaves
        // the birth window empty. It reduces to |1 - r| whenever the envelope
        // does not straddle 1, so the drift-free closed form is unchanged.
        const double w = wUp + wDown;
        const double requested = std::round(static_cast<double>(grainSeconds_) * sampleRate_);
        // THE GUARD IS SUBTRACTED TWICE, ONCE PER END, and the second one is
        // FR-014's. FR-025 spends `g` on the YOUNG side (a grain never reads
        // newer than age g). FR-014 independently requires
        // `getAvailableSamples() >= birth read age + kMinAgeSamples`, which -
        // once the ring is full, where available saturates at C - is an OLD-side
        // bound of a0 + dR <= C - g. The two rules are only jointly satisfiable
        // if the birth window is built to respect both, so `guard` appears at
        // both ends of the headroom here and again in ageHi below. Without it,
        // the corner where ceil(wDown*L') is small - captureSeconds = 1,
        // grainSeconds = 30, +24 semitones puts the window at [65530, 65534]
        // against C = 65536 - would demand 65598 available samples, which no
        // render can ever reach, and that configuration would be silent for
        // ever. It costs `g` = 64 samples of the ring: 0.012 % at the default
        // captureSeconds = 8 (C = 524288), and the FR-025 invariant is
        // STRENGTHENED by it, since the oldest reachable age becomes C - 2 - g
        // rather than C - 2.
        const double headroom = capacity - 2.0 - guard - guard - decorr;
        if (headroom <= 2.0) {
            ++skipRingCold_;
            return;
        }
        // TWO SAMPLES OF CEILING SLACK, RESERVED. Each ceil() below adds
        // strictly less than 1 to its argument, so their sum is < w*L' + 2.
        // Truncating against `headroom` itself would leave
        // aHi - aLo > headroom - w*L' - 2 >= -2: the window can invert by one
        // sample, and std::clamp(a0, lo, hi) with lo > hi is a PRECONDITION
        // VIOLATION - undefined behaviour that can still return a plausible a0.
        // Reserving two samples gives aHi - aLo > 0 for every envelope,
        // straddling or not, in one step. The SAME threshold gates the
        // non-truncating branch, because a lifetime in the gap (slack, headroom]
        // would carry the same one-sample inversion.
        const double slack = headroom - 2.0;
        const double lifetime = (w * requested > slack) ? std::floor(slack / w) : requested;
        // L' >= 2, not >= 1: FR-026's envelope phase denominator is L' - 1, so a
        // one-sample grain has no defined phase. A grain truncated to a single
        // sample is inaudible by construction, so rejecting it costs nothing.
        if (lifetime < 2.0) {
            ++skipRingCold_;
            return;
        }
        const double ageLo = std::ceil(wUp * lifetime) + guard;
        // The old-side `- guard` is FR-014's, see the headroom above. Window
        // non-emptiness still holds in ONE step: truncation gives
        // w*L' <= slack = C - 2 - 2g - dR - 2, and each ceil() adds < 1, so
        // ageHi - ageLo > (C - 2 - g - dR) - (w*L' + 2) - g >= 0.
        const double ageHi =
            capacity - 2.0 - guard - std::ceil(wDown * lifetime) - decorr;

        // --- (d) Birth read age (FR-029), clamped into that window.
        double birthAge =
            static_cast<double>(positionSeconds_) * sampleRate_ *
            (1.0 + (static_cast<double>(uPos) * static_cast<double>(positionSpread_)));
        birthAge = std::clamp(birthAge, ageLo, ageHi);

        // --- (e) FR-014 admission, VERBATIM: available >= birth read age + g.
        //     Checked AFTER the clamp so it tests the age actually used, and
        //     against birthAge + decorr because the right channel reads the
        //     older point - a strengthening, since FR-014 names one age and the
        //     grain has two.
        //
        //     THE MARGIN IS g = kMinAgeSamples = 64, which is what FR-014 says.
        //     It is only satisfiable because step (c) spends `guard` on the OLD
        //     side of the window as well: ageHi + dR <= C - 2 - g there, so
        //     `needed` is at most (C - 2 - g) + g = C - 2, and available
        //     saturates at C. Build the window without that term and this test
        //     becomes unreachable in the corner documented above.
        //
        //     The std::min is a ROUNDING GUARD on a theorem, not a relaxation:
        //     step (c) makes ageHi + dR == C - 2 - g - ceil(wDown*L') <=
        //     C - 2 - g exactly, so birthAge + dR can never really exceed
        //     C - 2 - g. But ageHi was FORMED by subtracting dR, and re-adding
        //     it can land ~1 ULP high - which at these magnitudes is enough to
        //     push the ceil() up by a whole sample and demand C + 1 available
        //     samples, i.e. reject that configuration in EVERY render for ever.
        //     Clipping to the bound the proof already establishes removes that
        //     without changing any admission the exact arithmetic would allow.
        const double oldestAge = std::min(birthAge + decorr, capacity - 2.0 - guard);
        const double needed = std::ceil(oldestAge) + guard;
        if (static_cast<double>(capture_.getAvailableSamples()) < needed) {
            ++skipRingCold_;
            return;
        }

        // --- (f) Equal-power pan (FR-032), the same law GrainProcessor uses
        //     (processors/grain_processor.h:101-103), computed ONCE. Hoisting
        //     these two transcendentals to birth is the single largest cost
        //     lever in the component; a regression that moved them per-sample
        //     would not change the output at all, only the CPU figure.
        const float pan = panSpread_ * uPan;
        const float panNorm = (pan + 1.0f) * 0.5f;
        const float panL = std::cos(panNorm * kHalfPi);
        const float panR = std::sin(panNorm * kHalfPi);

        // --- (h) Drift-lane birth semantics (FR-030). Zero lane `slot`'s walk
        //     state and DO NOT re-seed its stream. Done BEFORE the ratio is
        //     computed below, so every grain starts at exactly its snapshotted
        //     static pitch and drifts away over its life - there is no
        //     birth-time pitch step of up to +/-d from whatever value a
        //     free-running lane happened to hold. Deliberately NOT a
        //     BrownianDrift::reset() equivalent: that call re-seeds
        //     (processors/brownian_drift.h:133-135 -> :243), which would hand
        //     every grain on a slot one identical walk. Only prepare(), reset()
        //     and setSeed() re-seed a lane.
        driftLanes_.walk[slot] = 0.0f;
        driftLanes_.smoothCur[slot] = 0.0f;
        driftLanes_.smoothTgt[slot] = 0.0f;

        // --- (g) Commit. The ceil() form (not floor) is required so readFrac
        //     stays non-negative: ageL = (writeCounter_ - 1) - (readIndexInt +
        //     readFrac), so readIndexInt must sit at or BEFORE the target
        //     position and readFrac takes up the remainder. At the birth sample
        //     this reproduces ageL == birthAge exactly.
        const double ceilAge = std::ceil(birthAge);
        AtmosphereGrain& grain = grains_[slot];
        grain.readIndexInt = writeCounter_ - std::uint64_t{1} - static_cast<std::uint64_t>(ceilAge);
        grain.readFrac = static_cast<float>(ceilAge - birthAge);
        grain.staticSemis = staticSemis;
        grain.driftSemis = driftSemis;
        grain.semisLo = semisLo;
        grain.semisHi = semisHi;
        grain.ratioMin = ratioMin;
        grain.ratioMax = ratioMax;
        grain.decorrAge = decorrAge;
        grain.panL = panL;
        grain.panR = panR;
        grain.lifetime = static_cast<std::uint32_t>(lifetime);
        grain.envPhaseInc = 1.0f / static_cast<float>(lifetime - 1.0);
        grain.ageSamples = 0;
        grain.active = true;
        refreshGrainRatio(grain, slot);

        // Active list MAINTAINED by append here and swap-remove at retirement -
        // never rebuilt by scanning (contrast GrainPool::activeGrains(),
        // primitives/grain_pool.h:107-116, which GranularEngine calls once per
        // SAMPLE).
        activeIdx_[activeCount_] = static_cast<std::uint8_t>(slot);
        ++activeCount_;
        nextSlot_ = (slot + 1) % kMaxGrains;

        // --- (i) Introspection (FR-072).
        lastBirthAge_ = static_cast<float>(birthAge);
        lastBirthRatio_ = grain.ratio;
        lastBirthLifetime_ = static_cast<std::uint64_t>(lifetime);
        lastBirthSlot_ = slot;
        lastBirthPanL_ = panL;
        lastBirthPanR_ = panR;
        ++totalBorn_;
        foldObservedAge(static_cast<float>(birthAge));
        foldObservedAge(static_cast<float>(birthAge + decorr));
    }

    /// @brief Render ONE grain's contiguous span of chunk samples into the bus.
    ///        Phase 11.5 pass B's inner kernel - see renderGrainChunk's banner.
    ///
    /// Mutates `grain`'s read position and age in REGISTERS and stores them
    /// back once, which is the restructure's whole point: the former shape
    /// re-loaded and re-stored every field once per grain per sample.
    ///
    /// @param start      first chunk sample the grain sounds on
    /// @param spanEnd    one past its last sample (<= numSamples)
    /// @param retires    true iff spanEnd - 1 is the grain's retirement sample
    ///                   (folds the retirement age exactly as the former shape)
    /// @param numSamples the chunk length (for the S9.8 edge-fold identity and
    ///                   the reader rebase offset)
    /// @param chunkBase  newest absolute index AS OF SAMPLE 0 - every age is
    ///                   formed relative to EACH SAMPLE's write head
    ///                   (chunkBase + i) in the integer domain, bit-identical
    ///                   to the former per-sample expression; the end-of-chunk
    ///                   reader is index-rebased per sample to match
    void renderGrainSpan(AtmosphereGrain& grain, std::size_t start, std::size_t spanEnd,
                         bool retires, std::size_t numSamples,
                         const RollingCaptureBuffer::LinearReader& reader, const float* envelope,
                         std::uint64_t chunkBase) noexcept {
        // Register-resident hot state for the whole span.
        std::uint64_t readIndexInt = grain.readIndexInt;
        float readFrac = grain.readFrac;
        const float ratio = grain.ratio;  // held constant within a chunk (FR-024)
        const float envPhaseInc = grain.envPhaseInc;
        const float panL = grain.panL;
        const float panR = grain.panR;
        const float decorrAge = grain.decorrAge;
        std::uint32_t age = grain.ageSamples;

        for (std::size_t i = start; i < spanEnd; ++i) {
            // The age is formed against THIS SAMPLE's write head (chunkBase +
            // i) in the integer domain - BIT-IDENTICAL to the former
            // one-snapshot-per-sample expression, and that is load-bearing:
            // formed against the END-of-chunk head instead, the float
            // quantization of a large age becomes a function of where the
            // chunk boundary fell, and the render stops being
            // partition-invariant (measured: 3.3e-4 against the 1e-5 partition
            // bound). The difference is non-negative by construction (read age
            // >= kMinAgeSamples = 64 >= numSamples - also why pass A's
            // captures could be hoisted ahead of every read) and bounded by
            // C - 2 < 2^22, so the int64 -> float conversion is EXACT; the
            // SIGNED intermediate compiles to one vcvtsi2ss.
            const float ageNow =
                static_cast<float>(static_cast<std::int64_t>(
                    (chunkBase + static_cast<std::uint64_t>(i)) - readIndexInt)) -
                readFrac;

            // Envelope phase is MULTIPLIED, never accumulated: ageSamples is
            // exact to 2^24, so this costs one rounding, whereas a
            // `phase += 1/L'` accumulator over 1.44 M additions drifts by up
            // to ~4 % of full scale and would retire a grain at envelope
            // ~0.02 instead of 0 - a click.
            const float env = GrainEnvelope::lookup(
                envelope, kEnvelopeTableSize, static_cast<float>(age) * envPhaseInc);

            // The reader snapshot is END-of-chunk; the index is rebased by how
            // many samples newer than sample i that snapshot is, so position
            // and interpolation weights match the former per-sample snapshot
            // bit for bit.
            const std::size_t newerOffset = numSamples - 1u - i;
            float grainL = 0.0f;
            float grainR = 0.0f;
            reader.readStereoOffset(ageNow, newerOffset, grainL, grainR);
            if (decorrAge > 0.0f) {
                // The R channel reads a DIFFERENT point of the ring. Skipped
                // entirely at decorrelation = 0. readRightOffset() rather than
                // a second stereo read: the left half of that read was loaded,
                // interpolated and discarded.
                grainR = reader.readRightOffset(ageNow + decorrAge, newerOffset);
            }
            busL_[i] += env * panL * grainL;
            busR_[i] += env * panR * grainR;

            // S9.8's age fold: the chunk's first and last sample, plus the
            // retirement sample (the former `if (!foldNow)` retire fold - the
            // disjunction below folds each qualifying sample exactly once
            // either way). ageNow is already the per-sample-head value the
            // former shape folded.
            const bool edgeFold = (i == 0) || (i + 1 == numSamples);
            const bool retireFold = retires && (i + 1 == spanEnd);
            if (edgeFold || retireFold) {
                foldObservedAge(ageNow);
                if (decorrAge > 0.0f) {
                    foldObservedAge(ageNow + decorrAge);
                }
            }

            // Advance: integer + fraction, exact for the whole lifetime at
            // any rate. TRUNCATION, NOT std::floor: readFrac is non-negative
            // for the grain's whole life, so truncation toward zero IS the
            // floor - and std::floor(float) is a CRT call on MSVC's default
            // /arch. The carry is in [0, 8] because ratio <= 8.
            readFrac += ratio;
            const auto carryInt = static_cast<std::int32_t>(readFrac);
            readIndexInt += static_cast<std::uint64_t>(carryInt);
            readFrac -= static_cast<float>(carryInt);
            ++age;
        }

        grain.readIndexInt = readIndexInt;
        grain.readFrac = readFrac;
        grain.ageSamples = age;
    }

    /// @brief Capture, schedule and accumulate ONE control chunk into
    ///        busL_ / busR_ (plan S9.1, S9.2, S9.7, S9.8).
    ///
    /// @param numSamples Always <= kControlChunkSamples, because
    ///        processStereoBlock partitions on the absolute control grid.
    ///
    /// PHASE 11.5 GRAIN-SWEEP RESTRUCTURE (2026-08-04). The former shape was
    /// one per-SAMPLE loop sweeping every active grain: an AoS re-load, an
    /// envelope lookup, a ring read and a full state store per grain per
    /// sample, plus a fresh LinearReader snapshot per sample. Whole-process()
    /// attribution at the Seraphis 8-voice operating point measured that sweep
    /// at 25.4 % of one core - the single largest cost in the plugin - and the
    /// kMaxGrains banner's measurement shows the cost is instruction-bound, so
    /// the win is constant-factor: hold each grain's state in registers across
    /// the chunk (renderGrainSpan) instead of re-loading it per sample.
    ///
    /// THREE PASSES, SAME OBSERVABLE BEHAVIOUR:
    ///   A (per sample): pending in-chunk retirements (bookkeeping only, so a
    ///     slot freed at sample r is available to a birth from sample r + 1 -
    ///     the former availability exactly), then capture write, then
    ///     scheduler tick + birth. RNG draw order, every admission decision
    ///     (which reads getAvailableSamples() AS OF THAT SAMPLE) and every
    ///     counter are sample-exact. Retiring grains are SNAPSHOTTED
    ///     (RetiredGrainSpan) because a re-birth may overwrite the slot before
    ///     pass B renders it.
    ///   B (per grain): render each span into the zeroed bus. VALID because no
    ///     grain ever reads audio captured in this chunk: read age >=
    ///     kMinAgeSamples = 64 >= numSamples (FR-014's guard, established at
    ///     birth and preserved by the advance), so every position any read
    ///     touches is older than every pass-A write. Grains still active
    ///     render [birth offset, numSamples) and CANNOT retire here - every
    ///     in-chunk retirement was consumed by pass A.
    ///   C (per sample): the FR-028 population gain and the FR-063 poison
    ///     accumulator, verbatim (the smoother still advances exactly once per
    ///     output sample).
    ///
    /// WHAT IS AND IS NOT PRESERVED. RNG streams, admission decisions, grain
    /// trajectories, retirement/birth counters, end-of-chunk engine state,
    /// per-grain sample values (age formed against the PER-SAMPLE write head
    /// in the integer domain + the index-rebased reader keep position, weights
    /// and fold ages bit-identical - which is also what keeps the render
    /// PARTITION-invariant) and FR-071 determinism are EXACT. The one
    /// non-bit-exact residue: each output sample's contributions accumulate
    /// grain-by-grain into the bus instead of inside one per-sample register
    /// chain, so when a mid-chunk retirement re-orders the active list the
    /// addition ORDER can differ from the former shape - last-ULP rounding,
    /// inside every render-fingerprint tolerance.
    void renderGrainChunk(const float* inLeft, const float* inRight,
                          std::size_t numSamples) noexcept {
        // --- FR-063 input sanitiser, pass 1: numSamples adds, NO calls.
        //     isFinite() is deliberately non-inlinable (~2 ns), so a per-sample
        //     call would cost several percent of the whole CPU budget on its
        //     own. Nothing can hide from the sum: NaN and +/-Inf both propagate
        //     through `+`, and (+Inf) + (-Inf) is NaN - still non-finite - so no
        //     pair of non-finite samples can cancel out. A probe that overflows
        //     to +/-Inf from finite inputs merely takes the slow path, which is
        //     correct.
        float probe = 0.0f;
        for (std::size_t i = 0; i < numSamples; ++i) {
            probe += inLeft[i] + inRight[i];
        }
        const bool chunkClean = isFinite(probe);  // ONE call per chunk

        // --- PASS A: retirements due, capture, scheduler, births -------------
        // The due list holds every in-chunk retirement, sorted ascending by
        // retirement sample; equal keys keep activeIdx_ order, mirroring the
        // former sweep's same-sample retirement order.
        std::size_t dueCount = 0;
        for (std::size_t j = 0; j < activeCount_; ++j) {
            const std::size_t slot = activeIdx_[j];
            const AtmosphereGrain& g = grains_[slot];
            const auto remaining = static_cast<std::size_t>(g.lifetime - g.ageSamples);  // >= 1
            if (remaining <= numSamples) {
                const auto r = static_cast<std::uint32_t>(remaining - 1);
                std::size_t k = dueCount;
                while (k > 0 && dueScratch_[k - 1].r > r) {
                    dueScratch_[k] = dueScratch_[k - 1];
                    --k;
                }
                dueScratch_[k] = DueEntry{r, static_cast<std::uint8_t>(slot)};
                ++dueCount;
            }
        }
        std::size_t dueCursor = 0;
        std::size_t retiredCount = 0;

        // bornAt[slot] = birth offset + 1 within THIS chunk; 0 = active since
        // chunk start. 256 B of stack, zeroed per chunk.
        std::array<std::uint32_t, kMaxGrains> bornAt{};

        const auto bookkeepingRetire = [&](std::uint32_t r, std::size_t slot) noexcept {
            RetiredGrainSpan& e = retiredScratch_[retiredCount];
            ++retiredCount;
            e.grain = grains_[slot];
            e.start = (bornAt[slot] > 0u) ? (bornAt[slot] - 1u) : 0u;
            e.end = r + 1u;
            grains_[slot].active = false;
            // Counted INDEPENDENTLY here rather than derived as (born - active),
            // so `retired + active == born` is a real assertion instead of the
            // tautology a STEALING implementation would also satisfy.
            ++totalRetired_;
            for (std::size_t j = 0; j < activeCount_; ++j) {
                if (activeIdx_[j] == slot) {
                    activeIdx_[j] = activeIdx_[--activeCount_];  // swap-remove
                    break;
                }
            }
        };

        for (std::size_t i = 0; i < numSamples; ++i) {
            // Retire every grain whose final sample was i - 1: its slot is
            // available to a birth from THIS sample, the former availability
            // exactly.
            while (dueCursor < dueCount && dueScratch_[dueCursor].r + 1u == i) {
                bookkeepingRetire(dueScratch_[dueCursor].r, dueScratch_[dueCursor].slot);
                ++dueCursor;
            }

            // --- Capture, BEFORE any ring read for this sample (FR-012). That
            //     ordering IS the self-granulation the roadmap asks for. After
            //     the write the newest sample has absolute index
            //     writeCounter_ - 1 and age 0.
            float sampleL = inLeft[i];
            float sampleR = inRight[i];
            if (!chunkClean) {  // rare path: per-sample substitution, ring PRESERVED
                if (!isFinite(sampleL)) {
                    sampleL = 0.0f;
                }
                if (!isFinite(sampleR)) {
                    sampleR = 0.0f;
                }
            }
            capture_.writeStereo(sampleL, sampleR);  // rolling_capture_buffer.h:113
            ++writeCounter_;  // FR-013: monotonic uint64; getSamplesWritten()
                              // saturates at capacity (:119-121) and cannot serve

            // --- Scheduling (FR-021). GrainScheduler::process() draws exactly
            //     one rng value on a trigger (grain_scheduler.h:82). The
            //     admission tests inside tryBirthGrain() read the capture ring
            //     AS OF THIS SAMPLE - this pass stays per-sample for exactly
            //     that reason.
            if (scheduler_.process()) {
                const std::size_t before = activeCount_;
                tryBirthGrain();
                if (activeCount_ > before) {
                    const std::size_t slot = activeIdx_[activeCount_ - 1];
                    bornAt[slot] = static_cast<std::uint32_t>(i) + 1u;
                    // A newborn can retire inside this same chunk (lifetime is
                    // only bounded below by 2): insert its due entry into the
                    // unconsumed, still-sorted suffix.
                    const auto lifetime = static_cast<std::size_t>(grains_[slot].lifetime);
                    if (i + lifetime <= numSamples) {
                        const auto r = static_cast<std::uint32_t>(i + lifetime - 1u);
                        std::size_t k = dueCount;
                        while (k > dueCursor && dueScratch_[k - 1].r > r) {
                            dueScratch_[k] = dueScratch_[k - 1];
                            --k;
                        }
                        dueScratch_[k] = DueEntry{r, static_cast<std::uint8_t>(slot)};
                        ++dueCount;
                    }
                }
            }
        }
        // Retirements landing on the chunk's last sample(s) have no later
        // sample to be consumed on - drain them so end-of-chunk state matches
        // the former shape.
        while (dueCursor < dueCount) {
            bookkeepingRetire(dueScratch_[dueCursor].r, dueScratch_[dueCursor].slot);
            ++dueCursor;
        }

        // --- PASS B: the grain sweep, one grain across its whole span --------
        std::fill_n(busL_.data(), numSamples, 0.0f);
        std::fill_n(busR_.data(), numSamples, 0.0f);
        const float* envelope = activeEnvelope();
        // ONE ring-state snapshot per CHUNK (rolling_capture_buffer.h,
        // LinearReader), taken after pass A's final write: every position any
        // grain reads is >= kMinAgeSamples old and therefore untouched by pass
        // A's writes.
        const RollingCaptureBuffer::LinearReader reader = capture_.makeLinearReader();
        const std::uint64_t chunkBase =
            (writeCounter_ - std::uint64_t{1}) - static_cast<std::uint64_t>(numSamples - 1);

        for (std::size_t e = 0; e < retiredCount; ++e) {
            RetiredGrainSpan& span = retiredScratch_[e];
            renderGrainSpan(span.grain, span.start, span.end, /*retires=*/true, numSamples,
                            reader, envelope, chunkBase);
        }
        for (std::size_t j = 0; j < activeCount_; ++j) {
            const std::size_t slot = activeIdx_[j];
            AtmosphereGrain& grain = grains_[slot];
            const std::size_t start = (bornAt[slot] > 0u) ? (bornAt[slot] - 1u) : 0u;
            renderGrainSpan(grain, start, numSamples, /*retires=*/false, numSamples, reader,
                            envelope, chunkBase);
        }

        // --- PASS C: population gain + poison accumulator --------------------
        for (std::size_t i = 0; i < numSamples; ++i) {
            // --- FR-028: the 1/sqrt(n) population gain. ONE multiply on the
            //     SUMMED stereo bus, after every live grain has been
            //     accumulated - never captured per grain, which would leave a
            //     grain born into a crowd quiet for its whole 30 s life as the
            //     crowd thinned, and would invalidate SC-008's incoherent-sum
            //     argument (every grain must contribute with unit weight so the
            //     sum's variance is ~1 regardless of n).
            //
            //     The smoother is configured against the AUDIO rate (prepare()
            //     step 9) and advanced by exactly ONE process() per output
            //     sample. Advancing it once per control chunk instead would
            //     turn the 50 ms constant into 3.2 s, and the resulting level
            //     lag is not visibly wrong in any single test - which is why
            //     the cadence is stated rather than left to the reader.
            const float populationGain = gainSmoother_.process();
            const float busSampleL = busL_[i] * populationGain;
            const float busSampleR = busR_[i] * populationGain;
            busL_[i] = busSampleL;
            busR_[i] = busSampleR;

            // --- FR-063, internal path: TWO ADDS, NO CALLS. The pre-level bus
            //     is accumulated per sample and tested ONCE at the control-chunk
            //     boundary (runControlStep() step 6), which costs one isFinite()
            //     call per 64 samples instead of two per sample. Nothing can
            //     hide from the sum: NaN and +/-Inf both propagate through `+`,
            //     and (+Inf) + (-Inf) is NaN - still non-finite - so no pair of
            //     poisoned samples can cancel out. A false positive would need
            //     the accumulator to reach 3.4e38 from finite values inside 64
            //     samples; the bus is bounded well under 4.
            busPoisonAccum_ += busSampleL + busSampleR;
        }
    }

    /// @brief What happens on the 64-sample absolute control grid (FR-005).
    ///
    /// The ORDER is part of the determinism contract (FR-071). (Plan step 5,
    /// the age bookkeeping, is folded inside renderGrainChunk instead, at the
    /// chunk's first and last sample, because that is where the per-sample ages
    /// are already in hand - see S9.8 and foldObservedAge().)
    void runControlStep() noexcept {
        // 1. Push the control table into the scheduler. Cheap, idempotent, and
        //    it keeps interonsetSamples_ in step without a dirty flag. Doing it
        //    HERE rather than in the setters is what stops the render from
        //    depending on where a block boundary fell.
        scheduler_.setDensity(density_);
        scheduler_.setJitter(jitter_);

        // 2. Advance the OU bank by exactly one control chunk - TWO OU steps,
        //    because kDriftControlInterval = 32. This runs BEFORE any per-grain
        //    ratio refresh, so a grain born mid-chunk sees the lane value as of
        //    the most recent COMPLETED control step and birth timing inside a
        //    chunk cannot change the value it sees (FR-030).
        advanceDriftLanes(kControlChunkSamples);

        // 3. Per-grain ratio refresh from the JUST-ADVANCED lane values
        //    (FR-024, FR-030). Held constant for the whole chunk, which is what
        //    makes age(t) affine within a chunk and therefore makes S9.8's
        //    two-endpoint age fold exact rather than a sample.
        for (std::size_t j = 0; j < activeCount_; ++j) {
            const std::size_t slot = activeIdx_[j];
            refreshGrainRatio(grains_[slot], slot);
        }

        // 4. FR-028: refresh the population-gain TARGET from the LIVE count.
        //    max(1, n) keeps the empty pool at unity gain rather than at a
        //    division by zero, and it is the same expression the header's
        //    reset() snap (gainSmoother_.snapTo(1.0f)) assumes.
        gainSmoother_.setTarget(
            1.0f / std::sqrt(static_cast<float>(std::max(std::size_t{1}, activeCount_))));

        // 5. (Age bookkeeping is folded inside renderGrainChunk - see S9.8.)

        // 6. FR-063: evaluate the PREVIOUS chunk's poison accumulator, then zero
        //    it. ONE isFinite() call per 64 samples - the helper is deliberately
        //    non-inlinable (~2 ns) so that it survives /fp:fast, and two calls
        //    per sample would be ~4 % of the whole CPU budget for a test that
        //    fires essentially never.
        //
        //    An internal non-finite value fires silence(), so the grains retire
        //    under the FR-007 ramp and the engine LATCHES. There is no
        //    auto-resume: reset() (or a fresh prepare()) is the one documented
        //    recovery, exactly as for an explicit silence().
        chunkPoisoned_ = !isFinite(busPoisonAccum_);
        busPoisonAccum_ = 0.0f;
        if (chunkPoisoned_) {
            silence();
            chunkPoisoned_ = false;
        }
    }

    // =========================================================================
    // Spectral blur (plan S11)
    // =========================================================================

    /// @brief One control chunk of the blur stage: push, drain every complete
    ///        frame, pop exactly `numSamples` into the wet bus.
    ///
    /// THE LOOP ORDER IS LOAD-BEARING, NOT STYLE (FR-043). Three separate
    /// invariants live in it and each one fails silently if it is disturbed:
    ///
    /// 1. FRAME-MAJOR, CHANNEL-MINOR. The channel loop is INSIDE the frame loop,
    ///    so blurSmoother_ is advanced exactly once per hop of audio and the two
    ///    channels share ONE blur value. Both STFTs are pushed identical sample
    ///    counts, so canAnalyze() fires for L and R in lockstep and gating on
    ///    channel 0 gates the pair. Moving the advance inside the channel loop
    ///    advances the smoother 2 * blurHopSize_ per hop - halving the 50 ms
    ///    time constant to ~25 ms - and hands L and R values one hop apart
    ///    within the same frame. No criterion sweeping SETTLED blur can see it;
    ///    getAppliedBlur() and the settling clause in
    ///    AtmosphereEngine_ControlTableClamps exist for exactly this.
    ///
    /// 2. THE PULL IS INSIDE THE DRAIN LOOP. OverlapAdd::synthesize always
    ///    accumulates at outputBuffer_[0 .. fftSize) with NO offset
    ///    (stft.h:277-285); the per-frame hop offset comes ONLY from
    ///    pullSamples shifting the buffer left (:309-323). Two synthesize()
    ///    calls without an intervening pull of exactly hopSize stack both frames
    ///    at the same offset and destroy COLA - and the failure presents as a
    ///    windowing bug, not as a loop-order bug. The pull is always exactly
    ///    blurHopSize_, never numSamples: pullSamples returns SILENTLY when
    ///    numSamples > samplesReady_ (:306) without zeroing the destination, so
    ///    a wrong size is a stale-buffer read rather than a detectable failure.
    ///
    /// 3. L IS ALWAYS PROCESSED BEFORE R. Both channels draw from the one
    ///    blurRng_ stream, so the consumption order is part of the determinism
    ///    contract (FR-044/FR-071); swapping it changes the render.
    ///
    /// Push volume is bounded by the caller: processStereoBlock never hands this
    /// more than kControlChunkSamples at a time, so STFT::samplesAvailable_ stays
    /// at or below fftSize + 64 - well inside the 8 * fftSize input buffer that
    /// pushSamples fills with no overflow guard of its own (stft.h:104-113).
    void pumpBlur(std::size_t numSamples) noexcept {
        blurStft_[0].pushSamples(busL_.data(), numSamples);
        blurStft_[1].pushSamples(busR_.data(), numSamples);

        while (blurStft_[0].canAnalyze()) {
            // ONCE per frame-pair, OUTSIDE the channel loop, and BEFORE the
            // value is read (FR-009's cadence rule).
            blurSmoother_.advanceSamples(blurHopSize_);
            const float blurAmount = blurSmoother_.getCurrentValue();

            for (std::size_t ch = 0; ch < 2; ++ch) {
                blurStft_[ch].analyze(blurSpectrum_[ch]);

                // FR-042. MAGNITUDE IS NEVER WRITTEN - only the phase moves, so
                // the stage is a decoherer and not a filter. nextFloat() is
                // bipolar (random.h:59-63), so the perturbation is uniform on
                // +/-blur*pi: 0 is the identity and 1 is full decoherence.
                //
                // The loop runs k in [1, numBins-1) and DRAWS ONLY FOR THOSE
                // BINS: DC and Nyquist are skipped because their phase is not
                // free in a real spectrum, and consuming no draw for them is a
                // determinism decision (SC-010 pins the whole stream), not an
                // optimisation.
                //
                // The draw is PER BIN PER CHANNEL from the one blurRng_ stream,
                // which is what makes blur produce progressive stereo
                // decorrelation as well as fog. That is intended behaviour, not
                // a second width control (FR-060/N-9).
                SpectralBuffer& spectrum = blurSpectrum_[ch];
                const std::size_t numBins = spectrum.numBins();
                for (std::size_t k = 1; k + 1 < numBins; ++k) {
                    spectrum.setPhase(k, spectrum.getPhase(k) +
                                             blurAmount * kPi * blurRng_.nextFloat());
                }

                blurOla_[ch].synthesize(spectrum);
                blurOla_[ch].pullSamples(fifoScratch_[ch].data(), blurHopSize_);

                for (std::size_t i = 0; i < blurHopSize_; ++i) {
                    blurFifo_[ch][(blurFifoWrite_ + i) & blurFifoMask_] = fifoScratch_[ch][i];
                }
            }

            // The cursors are SHARED by the two channels (one ring geometry, two
            // data arrays), so they advance once per frame, after both writes.
            blurFifoWrite_ = (blurFifoWrite_ + blurHopSize_) & blurFifoMask_;
            blurFifoCount_ += blurHopSize_;
        }

        // Pop exactly numSamples. Post-reset the ring holds blurFftSize_ zeros
        // with write leading read by that occupancy, which is what makes
        // getLatencySamples() == blurFftSize_ the honest number and what keeps
        // the first block's pop from underflowing. The shortfall branch below
        // cannot be reached at any legal geometry (plan S11.4's occupancy
        // trace); it is defensive, not a designed path, and zero-fills rather
        // than reading a stale sample.
        for (std::size_t i = 0; i < numSamples; ++i) {
            if (blurFifoCount_ == 0) {
                wetL_[i] = 0.0f;
                wetR_[i] = 0.0f;
                continue;
            }
            wetL_[i] = blurFifo_[0][blurFifoRead_];
            wetR_[i] = blurFifo_[1][blurFifoRead_];
            blurFifoRead_ = (blurFifoRead_ + 1) & blurFifoMask_;
            --blurFifoCount_;
        }
    }

    // =========================================================================
    // Pure-freeze leg (plan S12.2)
    // =========================================================================

    /// @brief Render the pure-freeze drone for one control chunk into
    ///        freezeL_ / freezeR_, delay-matched to the blur latency (FR-052).
    ///
    /// TWO OSCILLATORS, NOT ONE. Both freeze() and processBlock() are MONO
    /// (processors/spectral_freeze_oscillator.h:217, :317), so L and R each own
    /// an instance prepared at the SNAPPED freeze FFT size.
    ///
    /// HARD BYPASS AT A SETTLED m == 0 ONLY, and there is deliberately NO
    /// symmetric bypass at a settled m == 1. At freezeMix = 1 the grain layer
    /// keeps running in full - scheduler, ageing, ring reads, 1/sqrt(n) and blur
    /// - even though its contribution is multiplied by zero, for two binding
    /// reasons: releasing the freeze is then seamless because the grain
    /// population never lapsed (a bypass would restart from an empty pool and
    /// swell back over density * grainSeconds seconds under FR-028's smoother),
    /// and the CPU criterion's frozen configuration measures the honest
    /// grain + freeze worst case instead of understating it. The m = 0 bypass is
    /// not symmetric with that because the oscillators hold nothing that has to
    /// stay warm, whereas the grain population does.
    ///
    /// THE DELAY IS STILL ADVANCED WITH ZEROS WHILE IN BYPASS. That costs one
    /// load and one store per sample per channel - about 1 % of the oscillator
    /// cost the bypass saves - and it is what makes LEAVING bypass click-free
    /// without an O(fftSize) memset spike on the audio thread. Pinned decision.
    ///
    /// Nothing here allocates: freezeDelay_ is sized in prepare() and the
    /// oscillator works entirely inside its own pre-allocated buffers.
    void renderFreezeChunk(std::size_t numSamples) noexcept {
        // FR-054: no oscillator, no capture scratch and no delay were allocated,
        // so the leg costs exactly nothing. freezeL_/freezeR_ stay at the zeros
        // reset() left them at, and finishChunk() never reads them.
        if (!freezeEnabled_) {
            return;
        }

        // The bypass test is chunk-rate and reads the ramp WITHOUT advancing it
        // - finishChunk() owns the per-output-sample advance. A target of 0 that
        // has not yet been reached still runs the oscillator, which is what
        // keeps the fade OUT of the drone audible rather than truncated.
        const bool settledDry = freezeMixRamp_.isComplete() && freezeMixRamp_.getTarget() == 0.0f;
        if (settledDry) {
            std::fill_n(freezeL_.data(), numSamples, 0.0f);
            std::fill_n(freezeR_.data(), numSamples, 0.0f);
        } else {
            // A never-captured oscillator fills zeros rather than stale audio
            // (spectral_freeze_oscillator.h:327-330), which is what makes the
            // bypassed and non-bypassed paths agree.
            freezeOsc_[0].processBlock(freezeL_.data(), numSamples);
            freezeOsc_[1].processBlock(freezeR_.data(), numSamples);
        }

        // With blur disabled the layer latency is 0, so there is nothing to
        // match and no ring was allocated (prepare() step 8).
        if (!blurEnabled_) {
            return;
        }
        for (std::size_t i = 0; i < numSamples; ++i) {
            const float delayedL = freezeDelay_[0][freezeDelayIdx_];
            const float delayedR = freezeDelay_[1][freezeDelayIdx_];
            freezeDelay_[0][freezeDelayIdx_] = freezeL_[i];
            freezeDelay_[1][freezeDelayIdx_] = freezeR_[i];
            freezeDelayIdx_ = (freezeDelayIdx_ + 1) & freezeDelayMask_;
            freezeL_[i] = delayedL;
            freezeR_[i] = delayedR;
        }
    }

    // =========================================================================
    // Output stage (plan S13.1)
    // =========================================================================

    /// @brief FR-052 crossfade, level trim, FR-007 silence ramp and FR-064
    ///        denormal flush for one control chunk: wetL_/wetR_ and
    ///        freezeL_/freezeR_ -> the caller's output.
    ///
    /// The crossfade is LINEAR, not equal-power: the two legs are independent
    /// signals, not two views of one, and SC-007's 0-detection clause is what
    /// gates the transition.
    ///
    /// THERE IS NO WIDTH CONTROL HERE (FR-060). No setWidth, no
    /// stereoCrossBlend, no core/stereo_utils.h: the stereo image comes solely
    /// from the per-grain equal-power pan (FR-032) and the per-grain L/R
    /// read-age decorrelation (FR-033). Global width is Phase 7's StereoField.
    /// Stated as a requirement rather than an omission so a later phase does not
    /// add one here "for symmetry" and end up with two controls on one axis.
    ///
    /// setLevel is the one output-stage control the engine keeps, because the
    /// 1/sqrt(n) grain sum is produced INSIDE the engine and a caller cannot
    /// trim it without a second pass over the buffer. It is a gain trim, not a
    /// dry/wet mix.
    void finishChunk(float* outLeft, float* outRight, std::size_t numSamples) noexcept {
        for (std::size_t i = 0; i < numSamples; ++i) {
            // The FR-007 latch can land MID-CHUNK. The remaining samples were
            // already rendered into the wet bus before the ramp reached 0, so
            // they must be written as exact zeros here - otherwise the first
            // block after the latch is silent by the entry guard while the
            // block the latch happened in still leaks post-latch audio.
            if (runState_ == RunState::Latched) {
                outLeft[i] = 0.0f;
                outRight[i] = 0.0f;
                continue;
            }

            // FR-052's crossfade, per OUTPUT sample, from the 100 ms LinearRamp.
            //
            // Gated on freezeEnabled_ so that a freezeMix stored on a
            // freeze-disabled engine is UNREAD rather than muting (FR-054) -
            // the same "the setter still clamps and stores, nothing reads it"
            // shape setBlur has when blur is off. At a settled m = 1 the wet
            // weight is EXACTLY 0.0f (LinearRamp::process clamps overshoot to
            // the target, primitives/smoother.h:380-383), so the grain layer -
            // which is still running in full, see renderFreezeChunk() -
            // contributes nothing and the output IS the drone.
            float sampleL = wetL_[i];
            float sampleR = wetR_[i];
            if (freezeEnabled_) {
                const float mix = freezeMixRamp_.process();
                const float wetGain = 1.0f - mix;
                sampleL = sampleL * wetGain + freezeL_[i] * mix;
                sampleR = sampleR * wetGain + freezeR_[i] * mix;
            }

            // FR-061: 20 ms, process() per OUTPUT sample against the audio rate.
            const float levelGain = levelSmoother_.process();
            sampleL *= levelGain;
            sampleR *= levelGain;

            if (runState_ == RunState::Silencing) {
                sampleL *= silenceGain_;
                sampleR *= silenceGain_;
                silenceGain_ -= silenceStep_;
                if (silenceGain_ <= 0.0f) {
                    latchNow();
                }
            }

            // FR-064: exact silence at level = 0, and no denormal ever leaves
            // the engine.
            outLeft[i] = detail::flushDenormal(sampleL);
            outRight[i] = detail::flushDenormal(sampleR);
        }
    }

    /// Phase 11.5 Step 0c. Close one stage-timer region: add the elapsed ns
    /// since `start` into `slot`, return a fresh start for the next region.
    /// Only reachable behind the processInstrumented_ gate.
    [[nodiscard]] static std::chrono::steady_clock::time_point stageLap(
        double& slot, std::chrono::steady_clock::time_point start) noexcept {
        const auto now = std::chrono::steady_clock::now();
        slot += std::chrono::duration<double, std::nano>(now - start).count();
        return now;
    }

    /// End of the FR-007 ramp: retire every grain and latch. totalRetired_ is
    /// incremented BEFORE activeCount_ is zeroed so FR-072's
    /// `retired + active == born` identity holds through the latch as well as
    /// through ordinary retirement.
    void latchNow() noexcept {
        silenceGain_ = 0.0f;
        totalRetired_ += static_cast<std::uint64_t>(activeCount_);
        for (auto& grain : grains_) {
            grain.active = false;
        }
        activeCount_ = 0;
        runState_ = RunState::Latched;
    }

    // =========================================================================
    // State
    // =========================================================================

    // --- capture -------------------------------------------------------------
    RollingCaptureBuffer capture_;
    std::uint64_t writeCounter_ = 0;    ///< total samples written, monotonic, never saturates
    std::size_t captureCapacity_ = 0;   ///< cached capture_.getCapacitySamples()
    float captureSeconds_ = 8.0f;       ///< validated PrepareConfig::captureSeconds
    std::size_t maxBlockSamples_ = 2048;///< validated; sizes the blur FIFO only

    // --- grains --------------------------------------------------------------
    std::array<AtmosphereGrain, kMaxGrains> grains_{};
    std::array<std::uint8_t, kMaxGrains> activeIdx_{};  ///< persistent, never rebuilt by scanning
    std::size_t activeCount_ = 0;

    /// Phase 11.5: a grain that retired mid-chunk (renderGrainChunk pass A),
    /// SNAPSHOTTED for pass B rendering - a snapshot and never a pointer,
    /// because the freed slot may be re-born within the same chunk and
    /// grains_[slot] overwritten before pass B runs.
    struct RetiredGrainSpan {
        AtmosphereGrain grain{};
        std::uint32_t start = 0;  ///< first chunk sample the span renders
        std::uint32_t end = 0;    ///< one past the retirement sample
    };
    /// A pending in-chunk retirement: grain in `slot` renders its last sample
    /// at chunk offset `r`.
    struct DueEntry {
        std::uint32_t r = 0;
        std::uint8_t slot = 0;
    };
    // Sized once in prepare() (2 * kMaxGrains each); indexed by count, never
    // pushed on the audio thread.
    std::vector<RetiredGrainSpan> retiredScratch_;
    std::vector<DueEntry> dueScratch_;
    std::size_t nextSlot_ = 0;  ///< FR-020 round-robin cursor
    GrainScheduler scheduler_;
    GrainDriftLanes driftLanes_;
    float driftSmoothCoeff_ = 0.0f;  ///< calculateOnePolCoefficient(150 ms, sr)

    // --- envelope ------------------------------------------------------------
    /// kEnvelopeTypeCount conditioned windows of kEnvelopeTableSize entries,
    /// laid out back to back in GrainEnvelopeType order. Allocated and filled in
    /// prepare(); never resized and never rewritten afterwards.
    std::vector<float> envelopeTable_;
    GrainEnvelopeType envelopeType_ = GrainEnvelopeType::Hann;

    // --- blur ----------------------------------------------------------------
    std::array<STFT, 2> blurStft_;
    std::array<OverlapAdd, 2> blurOla_;
    std::array<SpectralBuffer, 2> blurSpectrum_;
    // The blur output FIFO re-times the stage from HOP-sized bursts to the
    // caller's chunk size; OverlapAdd's own 2*fftSize buffer cannot serve,
    // because pullSamples consumes it with a shift-left (stft.h:309-323).
    //
    // RING INVARIANT, maintained by every push and pop and established by
    // reset():
    //     blurFifoWrite_ == (blurFifoRead_ + blurFifoCount_) & blurFifoMask_
    //
    // A push writes at blurFifoWrite_ and advances it under the mask while
    // incrementing the count; a pop reads at blurFifoRead_ and advances it under
    // the mask while decrementing the count - so both preserve it by
    // construction. The ONLY way to break it is to initialise the three fields
    // inconsistently, which is why reset() sets
    // blurFifoWrite_ = blurFftSize_ & blurFifoMask_ rather than 0 alongside the
    // blurFftSize_ pre-fill occupancy. The three cursors are SHARED by the two
    // channels: one ring geometry, two data arrays, always written and read at
    // the same offsets.
    std::array<std::vector<float>, 2> blurFifo_;      ///< power-of-two re-timing ring
    std::array<std::vector<float>, 2> fifoScratch_;   ///< blurHopSize_ pull target
    std::size_t blurFifoMask_ = 0;
    std::size_t blurFifoWrite_ = 0;
    std::size_t blurFifoRead_ = 0;
    std::size_t blurFifoCount_ = 0;
    Xorshift32 blurRng_{1};      ///< FR-044: SEPARATE from grainRng_
    OnePoleSmoother blurSmoother_;
    std::size_t blurFftSize_ = 0;
    std::size_t blurHopSize_ = 0;
    bool blurEnabled_ = false;

    // --- freeze --------------------------------------------------------------
    std::array<SpectralFreezeOscillator, 2> freezeOsc_;
    std::array<std::vector<float>, 2> freezeCapture_;  ///< getFftSize() scratch
    std::array<std::vector<float>, 2> freezeDelay_;    ///< blurFftSize-sample ring
    std::size_t freezeDelayMask_ = 0;
    std::size_t freezeDelayIdx_ = 0;
    std::size_t freezeFftSize_ = 0;
    LinearRamp freezeMixRamp_;
    bool freezeEnabled_ = false;

    // --- output --------------------------------------------------------------
    OnePoleSmoother gainSmoother_;   ///< 1/sqrt(n) population compensation
    OnePoleSmoother levelSmoother_;
    float silenceGain_ = 1.0f;
    float silenceStep_ = 0.0f;
    RunState runState_ = RunState::Running;
    /// Per-chunk sum of the pre-level bus: ONE isFinite() call per 64 samples
    /// instead of two per sample. Non-finites propagate through `+` and
    /// (+Inf)+(-Inf) is NaN, so nothing cancels and no transient is missed.
    float busPoisonAccum_ = 0.0f;
    bool chunkPoisoned_ = false;

    // --- control values (FR-009), all plain scalars --------------------------
    float grainSeconds_ = 4.0f;
    float density_ = 4.0f;
    float jitter_ = 0.5f;
    float positionSeconds_ = 1.0f;
    float positionSpread_ = 0.3f;
    float pitchSemitones_ = 0.0f;
    float pitchSpread_ = 0.15f;
    float driftDepth_ = 0.3f;
    float driftSmoothness_ = 0.7f;
    float driftRangeSemitones_ = 2.0f;
    float panSpread_ = 0.7f;
    float decorrelation_ = 0.5f;
    /// The smoothers' TARGETS, kept so reset() can snap without re-deriving.
    float blur_ = 0.0f;
    float freezeMix_ = 0.0f;
    float level_ = 1.0f;
    Xorshift32 grainRng_{1};
    std::uint32_t seed_ = kDefaultSeed;

    // --- clock, scratch, introspection ---------------------------------------
    double sampleRate_ = 44100.0;
    std::uint64_t sampleCounter_ = 0;  ///< FR-005's ABSOLUTE control grid anchor
    bool prepared_ = false;
    std::array<float, kControlChunkSamples> busL_{};     // grain sum
    std::array<float, kControlChunkSamples> busR_{};
    std::array<float, kControlChunkSamples> wetL_{};     // post-blur
    std::array<float, kControlChunkSamples> wetR_{};
    std::array<float, kControlChunkSamples> freezeL_{};  // oscillator output
    std::array<float, kControlChunkSamples> freezeR_{};
    std::uint64_t skipPoolFull_ = 0;
    std::uint64_t skipRingCold_ = 0;
    std::uint64_t totalBorn_ = 0;
    std::uint64_t totalRetired_ = 0;
    float minObservedAge_ = 0.0f;
    float maxObservedAge_ = 0.0f;
    float lastBirthAge_ = 0.0f;
    float lastBirthRatio_ = 1.0f;
    std::uint64_t lastBirthLifetime_ = 0;
    std::size_t lastBirthSlot_ = 0;
    float lastBirthPanL_ = 1.0f;
    float lastBirthPanR_ = 1.0f;

    // --- Phase 11.5 Step 0c stage timers (test-only; see ProcessStage) --------
    bool processInstrumented_ = false;
    std::array<double, static_cast<std::size_t>(ProcessStage::Count)> processStageNs_{};
};

}  // namespace DSP
}  // namespace Krate
