// ==============================================================================
// Layer 3: System Component - NoiseOrganism
// ==============================================================================
// Living noise: up to four semi-independent noise sources, each feeding its own
// resonator -> comb -> stochastic-filter chain whose parameters wander on slow,
// bounded, individually seeded modulation lanes. Vorago's second sound source
// beside the harmonic cloud.
//
// Feature: vorago-phase2-noise-organism
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, prepare() is the only allocator)
// - Principle III: Modern C++ (C++20, RAII, value semantics, no raw owning ptrs)
// - Principle IX: Layer 3 (composes Layer 0-2 components + one same-layer peer)
// - Principle X: DSP Constraints (bounded stochastic motion, de-zippered gains)
// - Principle XI: Performance Budget (<1% CPU per voice at 48 kHz)
//
// Reference: specs/vorago-phase2-noise-organism/spec.md
//            specs/vorago-phase2-noise-organism/plan.md
//
// BUILD STATE (tasks.md T016 - the calibration pass):
//   The control surface, the lifecycle (prepare / reset / setSeed /
//   applyConfiguration), processBlock's guard ladder, the ABSOLUTE 64-sample
//   control grid, the per-slot render chain
//   (source -> resonators -> combs -> filter -> gain, plus the FR-074 tail), the
//   wander lanes (fixed 64-sample advance in updateControl, the FR-069
//   seconds->normalised rate mapping, the FR-068 master switch) AND the gain
//   chain (the two per-sample LinearRamps, the FR-070 affine breathing map and
//   the FR-013 change-detected coalescing duck FSM) are real. So are all three
//   composed models. Two of them are pure CONFIGURATION of the parts above and
//   add no DSP at all (FR-020, FR-040):
//     * FilteredWind - base type pinned to Brown (effectiveNoiseType), chain
//       filter switched to SVFMode::Bandpass over a 2-octave range with 400 ms
//       smoothing (applySlotConfiguration).
//     * MetallicHiss - base type pinned to Blue, or Violet via setHissBright;
//       comb feedback defaulting to kMetallicCombFeedback; inharmonic comb
//       ratios the organism evaluates itself (updateCombBaseDelays).
//   The third is the one model with DSP of its own:
//     * GranularDust - the slot's Velvet train TRIGGERS a 24-grain pool
//       (acquireGrain / renderDust) that windows a NoiseOscillator carrier with
//       the shared Hann table; concurrency is bounded bidirectionally by
//       updateDustEffective and each grain's gain and phase increment are locked
//       at birth (FR-030..FR-036).
//   All four calibration tables are now MEASURED, not authored, by the hidden
//   `NoiseOrganism_MeasureSourceDrive` case (tagged [.calibration]) in
//   dsp/tests/unit/systems/noise_organism_perf_test.cpp, which stays checked in
//   so a guessed table cannot be shipped: kSourceDriveDb (FR-017), kModelTrimDb
//   (FR-017), kMakeupSlopeDb (FR-018) and kMaxCombDelayStepSamples (FR-063).
//   Each carries its method and the 2026-09-01 measurement date beside it.
//
//   VERIFIED WITH THOSE TABLES IN PLACE (48 kHz, seed 0x5EEDBEEF):
//     * SC-019 (a): all 15 cells land inside their +/-3 dB window. The 12
//       Direct type cells are within +/-1.17 dB of the White reference in the
//       chain-neutralised fixture (which is where kSourceDriveDb, a generator-
//       side constant, is calibrated), once FR-013's setTapeHissParams /
//       setAsperityParams forwards are applied (kSignalDependentFloorDb). The 3
//       composed-model cells are measured in the SC-004 (c) REFERENCE CHAIN,
//       against a Direct slot in that same chain - the fixture kModelTrimDb is
//       calibrated in, because for a composed model the chain IS the model -
//       and land within 1.3 dB of it. The level-ownership arm measures
//       exactly 12.000 dB.
//     * SC-001 (c): the FR-016 default configuration renders -53.5 / -49.0 /
//       -45.8 dBFS at 1 / 2 / 4 sources, inside (-60, -3], with ZERO FR-074
//       clamp engagements. In the reference chain every model now renders
//       within 0.003 dB of the Direct slot's -50.6 dBFS (kModelTrimDb).
//
//   *** ONE OPEN ITEM, MEASURED AND ESCALATED - NOT A DEFECT AN AGENT MAY CLOSE:
//     (CLOSED 2026-09-01) TapeHiss and Asperity could not be calibrated inside
//        NoiseGenerator's [-96, +12] level range while the organism left their
//        floors at the library defaults, and both fell below SC-019 (a)'s
//        -60 dBFS non-silence floor. Root cause was a MISSING FR-013 forward,
//        not an unresolvable conflict: FR-013 requires setTapeHissParams /
//        setAsperityParams to be forwarded and applySlotConfiguration did not.
//        With the forward in place both drives are in range (+3.690 / +0.000)
//        and both cells clear the criterion. See kSignalDependentFloorDb.
//     (CLOSED 2026-09-01) A MetallicHiss slot at the FR-016 CHAIN defaults
//        rendered -86.8 dBFS - inaudible - because the model pins Blue noise,
//        which has almost no energy at the FR-016 resonator anchors, and the
//        chain-neutralised trim could not see a chain it did not run. Same root
//        cause as the level non-stationarity Group Q found: kModelTrimDb was
//        measured in the wrong fixture. Re-measured in the SC-004 (c) reference
//        chain (see kModelTrimDb), all four models now render within 0.03 dB of
//        each other there and the MetallicHiss slot lands at -50.6 dBFS.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>             // L0 detail::isFinite, flushDenormal
#include <krate/dsp/core/grain_envelope.h>       // L0 GrainEnvelope::generate / ::lookup
#include <krate/dsp/core/pattern_freeze_types.h> // L0 NoiseColor
#include <krate/dsp/core/random.h>               // L0 Xorshift32, deriveStreamSeed
// L1 - nextPowerOf2 (delay_line.h:26), the one piece of the S12 memory formula
// getAllocatedBytes() must reproduce exactly: TimeVaryingCombBank sizes every
// one of its kMaxCombs lines through DelayLine::prepare (delay_line.h:267-278).
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/noise_oscillator.h>    // L1 dust carrier
#include <krate/dsp/primitives/smoother.h>            // L1 LinearRamp (level + gate ramps)
#include <krate/dsp/primitives/svf.h>                 // L1 SVFMode, SVF::kMinQ/kMaxQ
#include <krate/dsp/processors/breathing_modulator.h> // L2
#include <krate/dsp/processors/brownian_drift.h>      // L2
#include <krate/dsp/processors/noise_generator.h>     // L2
#include <krate/dsp/processors/perlin_noise_source.h> // L2
#include <krate/dsp/processors/resonator_bank.h>      // L2
#include <krate/dsp/processors/stochastic_filter.h>   // L2
// L3 - same layer, permitted: tools/lint-layers.js fails only on an UPWARD reach.
// Precedent: systems/continuous_body.h:42 includes this exact header the same way.
#include <krate/dsp/systems/timevar_comb_bank.h>      // L3

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// NoiseOrganismModel (FR-011)
// =============================================================================
// APPEND ONLY - NEVER REORDER. The numeric values are the on-the-wire encoding
// a later phase will persist in plugin state; renumbering one enumerator would
// silently rewrite every stored preset.
// =============================================================================
enum class NoiseOrganismModel : std::uint8_t {
    Direct       = 0,  ///< The slot's selected NoiseType, default chain.
    FilteredWind = 1,  ///< Brown noise through a band-pass StochasticFilter.
    GranularDust = 2,  ///< Velvet impulses windowing a noise carrier.
    MetallicHiss = 3   ///< Blue/Violet through inharmonically tuned combs.
};

// =============================================================================
// NoiseOrganism (FR-001)
// =============================================================================
class NoiseOrganism {
public:
    // -------------------------------------------------------------------------
    // Capacities and constants (FR-010, FR-034, FR-051, FR-054, FR-069, FR-074)
    // -------------------------------------------------------------------------
    /// Maximum number of noise slots (roadmap: "N (2-4) simultaneous sources").
    static constexpr std::size_t kMaxSources = 4;
    /// Resonators enabled per slot, out of ResonatorBank's 16 (FR-051).
    static constexpr std::size_t kMaxResonatorsPerSource = 4;
    /// Combs enabled per slot, out of TimeVaryingCombBank's 8 (FR-054).
    static constexpr std::size_t kMaxCombsPerSource = 4;
    /// Granular-dust grain pool, per slot (FR-034).
    static constexpr std::size_t kMaxDustGrains = 24;
    /// Control-grid period in samples (FR-007).
    static constexpr std::size_t kControlChunkSamples = 64;
    /// Shared Hann grain-window table length (FR-033).
    static constexpr std::size_t kDustEnvelopeTableSize = 2048;

    // The control clock is shared library-wide, not a local choice: HarmonicCloud
    // uses the same period (harmonic_cloud.h:144) and ContinuousBody
    // static_asserts on it (continuous_body.h:630). A component that drifted off
    // it would decorrelate the per-voice modulation grid in Vorago Phase 10.
    static_assert(kControlChunkSamples == 64,
                  "control chunk must stay at the shared 64-sample grid "
                  "(harmonic_cloud.h:144)");

    /// Organism-wide default wander rate, in Hz (FR-069).
    static constexpr float kDefaultWanderRateHz = 0.03f;
    /// Stability cap on comb feedback - below the bank's own limit (FR-090).
    static constexpr float kCombFeedbackCap = 0.9f;
    /// Comb feedback a slot runs at until a caller writes one (FR-016).
    static constexpr float kDefaultCombFeedback = 0.55f;
    /// ...except on MetallicHiss, whose inharmonic ring IS the model (FR-042),
    /// so it defaults hotter. Still a DEFAULT: setCombFeedback outranks it.
    static constexpr float kMetallicCombFeedback = 0.75f;
    static_assert(kMetallicCombFeedback <= kCombFeedbackCap,
                  "a per-model default must sit inside the FR-090 cap");
    /// Half-span of the FR-070 affine breathing map.
    static constexpr float kBreathGainSpan = 0.45f;
    /// Maximum downward Q factor span of the FR-064 Q lane.
    static constexpr float kQWanderSpan = 0.9f;
    /// Duration of every gain ramp, in ms (FR-013, FR-073).
    static constexpr float kGainRampMs = 50.0f;
    /// Hard output bound (FR-074).
    static constexpr float kOutputClamp = 4.0f;

    // -------------------------------------------------------------------------
    // PrepareConfig (FR-002) - AtmosphereEngine::PrepareConfig shape
    // (atmosphere_engine.h:369-376).
    //
    // Callers MUST use designated initialisers - `PrepareConfig{.numSources = 3}`
    // - so that no narrowing conversion can hide in a positional brace init
    // (FR-094; Clang errors where MSVC does not).
    // -------------------------------------------------------------------------
    struct PrepareConfig {
        std::size_t maxBlockSamples = 2048;   ///< Clamped to [64, 8192].
        float       maxCombDelayMs  = 50.0f;  ///< Clamped to [5, 200].
        std::size_t numSources      = 2;      ///< Clamped to [1, kMaxSources].
    };

    // -------------------------------------------------------------------------
    // DustGrain (FR-034) - one slot of the granular-dust pool.
    //
    // Nested deliberately: `Grain` (primitives/grain_pool.h:23) and the
    // `GrainEnvelope` namespace (core/grain_envelope.h:23) already exist at
    // namespace scope, so no new top-level grain name is claimed here.
    // -------------------------------------------------------------------------
    struct DustGrain {
        float phase          = 0.0f;   ///< Normalised window position [0, 1).
        float phaseIncrement = 0.0f;   ///< Per-sample window advance.
        float gain           = 0.0f;   ///< Fixed amplitude for this grain.
        bool  active         = false;  ///< Slot occupancy.
    };

    NoiseOrganism() noexcept = default;
    // Deep buffers; never copy on the RT thread.
    NoiseOrganism(const NoiseOrganism&)            = delete;
    NoiseOrganism& operator=(const NoiseOrganism&) = delete;
    NoiseOrganism(NoiseOrganism&&) noexcept            = default;
    NoiseOrganism& operator=(NoiseOrganism&&) noexcept = default;

    // =========================================================================
    // Lifecycle (FR-002 .. FR-005)
    // =========================================================================

    /// @brief Size every buffer and pool. THE ONLY METHOD ALLOWED TO ALLOCATE.
    /// Re-preparing is legal and returns every setting to its FR-016 default -
    /// reset() (FR-004) deliberately does not.
    /// @param sampleRate Hz; non-finite is substituted by 48000.0, then floored at 1.
    /// @param config Capacity/sizing surface; each field is clamped (FR-002).
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        sampleRate_ = std::max(1.0, sanitise(sampleRate, 48000.0));
        config_.maxBlockSamples =
            std::clamp(config.maxBlockSamples, std::size_t{64}, std::size_t{8192});
        config_.maxCombDelayMs =
            std::clamp(sanitise(config.maxCombDelayMs, 50.0f), 5.0f, 200.0f);
        config_.numSources = std::clamp(config.numSources, std::size_t{1}, kMaxSources);

        const auto sampleRateF = static_cast<float>(sampleRate_);
        rampSamples_ = std::max<std::size_t>(
            std::size_t{1},
            static_cast<std::size_t>(std::lround(
                static_cast<double>(kGainRampMs) * 0.001 * sampleRate_)));

        // One shared window table for every slot's dust engine (FR-033).
        GrainEnvelope::generate(dustEnvelope_.data(), kDustEnvelopeTableSize,
                                GrainEnvelopeType::Hann);

        // EVERY slot is prepared, not only the config_.numSources active ones: a
        // later setNumSources() must never need a re-prepare (FR-010).
        for (Slot& slot : slots_) {
            // The ONE narrowing cast in this component, here and nowhere in the
            // render path - NoiseGenerator::prepare takes a float
            // (noise_generator.h:135); every other sub-component takes a double.
            slot.generator.prepare(sampleRateF, config_.maxBlockSamples);
            slot.carrier.prepare(sampleRate_);
            slot.resonators.prepare(sampleRate_);
            slot.combs.prepare(sampleRate_, config_.maxCombDelayMs);
            slot.filter.prepare(sampleRate_, config_.maxBlockSamples);
            for (BrownianDrift& lane : slot.resFreqLane) {
                lane.prepare(sampleRate_);
            }
            slot.cutoffLane.prepare(sampleRate_);
            slot.resonanceLane.prepare(sampleRate_);
            for (PerlinNoiseSource& lane : slot.combLane) {
                lane.prepare(sampleRate_);
            }
            slot.breathing.prepare(sampleRate_);
        }

        // prepare() is the ONLY path back to the FR-016 defaults (FR-002).
        applyDefaults();

        // Set BEFORE applyConfiguration(): that routine is a silent no-op on an
        // un-prepared organism (so a setter called before prepare() can never
        // reach an un-prepared sub-component), and every sub-component has just
        // been prepared above.
        prepared_ = true;
        applyConfiguration();

        controlPhase_     = 0;
        clampEngagements_ = 0;
        scratchA_.fill(0.0f);
        scratchB_.fill(0.0f);

        // prepare() and reset() must converge on ONE state - FR-004's
        // "configuration-preserving" is only meaningful if the state reset()
        // produces is the state prepare() produces, and SC-006 (a) asserts
        // exactly that, sample for sample. Two sub-components do not get there on
        // their own; each helper documents which and why.
        snapChainFiltersToConfiguration();
        settleSourceLevelSmoothers();

        for (std::size_t i = 0; i < kMaxSources; ++i) {
            Slot& slot = slots_[i];
            slot.levelRamp.configure(kGainRampMs, sampleRateF);
            slot.levelRamp.snapTo(dbToGain(slot.levelDb));
            slot.gate.configure(kGainRampMs, sampleRateF);
            slot.gate.snapTo(gateSteady(i));
        }

        allocatedBytes_ = computeAllocatedBytes();

        // LAST, deliberately: NoiseGenerator::prepare ends with reset()
        // (noise_generator.h:182), which scrambles the RNG on an un-latched
        // instance (:189) - a seed distributed any earlier would be discarded.
        setSeed(seed_);
    }

    /// @brief Clear all audio state, preserving configuration (FR-004).
    /// Load-bearing detail for the task that fills this in: ResonatorBank::reset()
    /// is a CONFIGURATION WIPE (resonator_bank.h:226-232, doc at :212), so the
    /// organism must re-apply its configuration afterwards or every slot with
    /// resonators enabled renders silence.
    void reset() noexcept {
        controlPhase_     = 0;
        clampEngagements_ = 0;
        scratchA_.fill(0.0f);
        scratchB_.fill(0.0f);
        for (Slot& slot : slots_) {
            slot.generator.reset();
            slot.carrier.reset();
            slot.resonators.reset();
            slot.combs.reset();
            slot.filter.reset();
            for (BrownianDrift& lane : slot.resFreqLane) {
                lane.reset();
            }
            slot.cutoffLane.reset();
            slot.resonanceLane.reset();
            for (PerlinNoiseSource& lane : slot.combLane) {
                lane.reset();
            }
            slot.breathing.reset();

            slot.grains.fill(DustGrain{});
            slot.grainCursor       = 0;
            slot.sourceRmsSmoothed = 0.0f;
            slot.sourceSumSq       = 0.0f;
            slot.sourceSumCount    = 0;
            slot.breathGain        = 1.0f;

            // A change still in flight is a REQUESTED configuration, and reset()
            // is configuration-preserving (FR-004) - so it is APPLIED here rather
            // than discarded, or the write would be lost and the FR-015 getters
            // would report a value the caller never asked for. No click concern:
            // every audio state in the component is being cleared in this same
            // call. The applyConfiguration() below pushes the result.
            if (slot.duckPending) {
                slot.model         = slot.pendingModel;
                slot.requestedType = slot.pendingType;
                slot.activeType =
                    effectiveNoiseType(slot.model, slot.requestedType, slot.hissViolet);
            }
            slot.duckState   = Duck::Idle;
            slot.duckPending = false;
        }

        // MANDATORY, not an optimisation: ResonatorBank::reset() is a
        // CONFIGURATION WIPE (440 Hz, default decay, unity gain, default Q and
        // enabled_[i] = false - resonator_bank.h:226-232, doc at :212), so a
        // forwarded reset without re-application renders SILENCE on every slot
        // that has resonators enabled. TimeVaryingCombBank::reset() re-seeds its
        // per-comb RNGs and snaps its smoothers (:482-495), so it needs the
        // delay/feedback re-push too.
        applyConfiguration();

        // ...and the chain filter, whose own reset() ran above against the last
        // DRIFTED base rather than the configured one. Same call, same position
        // relative to applyConfiguration(), as prepare() makes (SC-006 (a)).
        snapChainFiltersToConfiguration();

        // NoiseGenerator::reset() scrambles unless the FR-081 latch is set
        // (noise_generator.h:186-193), and the latch only replays the LATCHED
        // value - so the derived per-slot seeds must be re-asserted here for the
        // stream to be reproducible.
        setSeed(seed_);

        const auto sampleRateF = static_cast<float>(sampleRate_);
        for (std::size_t i = 0; i < kMaxSources; ++i) {
            Slot& slot = slots_[i];
            slot.levelRamp.configure(kGainRampMs, sampleRateF);
            slot.levelRamp.snapTo(dbToGain(slot.levelDb));
            slot.gate.configure(kGainRampMs, sampleRateF);
            slot.gate.snapTo(gateSteady(i));
        }
    }

    /// @brief Seed every internal stream from one organism seed (FR-005).
    /// A seed of 0 is legal: deriveStreamSeed substitutes 0x2545F491u for a zero
    /// hash (core/random.h:112) and Xorshift32::seed substitutes its own default
    /// for 0 (random.h:44-45), so no lane can collapse onto a degenerate stream.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        for (std::size_t s = 0; s < kMaxSources; ++s) {
            Slot& slot = slots_[s];
            slot.generator.setSeed(deriveStreamSeed(seed_, kSaltNoiseGen + s));
            slot.carrier.setSeed(deriveStreamSeed(seed_, kSaltDustCarrier + s));
            slot.filter.setSeed(deriveStreamSeed(seed_, kSaltChainFilter + s));
            for (std::size_t i = 0; i < kMaxResonatorsPerSource; ++i) {
                slot.resFreqLane[i].setSeed(deriveStreamSeed(
                    seed_, kSaltResonatorLane + s * kMaxResonatorsPerSource + i));
            }
            slot.cutoffLane.setSeed(deriveStreamSeed(seed_, kSaltFilterCutoff + s));
            slot.resonanceLane.setSeed(deriveStreamSeed(seed_, kSaltFilterReso + s));
            for (std::size_t i = 0; i < kMaxCombsPerSource; ++i) {
                slot.combLane[i].setSeed(deriveStreamSeed(
                    seed_, kSaltCombLane + s * kMaxCombsPerSource + i));
            }
            slot.breathing.setSeed(deriveStreamSeed(seed_, kSaltBreathing + s));
        }
    }

    /// @brief Render mono audio, OVERWRITING `output` (FR-003).
    /// Guard ladder, in order (harmonic_cloud.h:880-891): null output writes
    /// nothing and advances nothing; numSamples == 0 is a no-op consuming no
    /// control step; an un-prepared organism fills exactly numSamples zeros and
    /// advances nothing.
    void processBlock(float* output, std::size_t numSamples) noexcept {
        if (output == nullptr) {
            return;  // nothing written, NOTHING advanced
        }
        if (numSamples == 0) {
            return;  // no control step consumed, buffer untouched
        }
        if (!prepared_) {
            std::fill_n(output, numSamples, 0.0f);  // exactly numSamples zeros
            return;                                 // ...and no state advance
        }

        // ABSOLUTE 64-sample control grid. controlPhase_ is a RESIDUE carried
        // across calls, deliberately NOT the block-relative counter HarmonicCloud
        // uses (harmonic_cloud.h:144's chunking is `min(64, numSamples - done)`
        // measured from the block start): that runs TWO control steps for a 36+28
        // split where an unsplit 64 runs one, and SC-016 demands
        // max|difference| == 0 across the partition 36, 28, 1000, 1, 511, 2048.
        std::size_t done = 0;
        while (done < numSamples) {
            if (controlPhase_ == 0) {
                updateControl();
            }
            const std::size_t chunk =
                std::min(numSamples - done, kControlChunkSamples - controlPhase_);
            renderChunk(output + done, chunk);
            controlPhase_ = (controlPhase_ + chunk) % kControlChunkSamples;
            done += chunk;
        }
    }

    // =========================================================================
    // Slots (FR-010 .. FR-014)
    // =========================================================================

    /// @brief Number of active slots, clamped to [1, kMaxSources] (FR-010).
    /// Never reallocates; dropped slots are silenced (FR-072).
    void setNumSources(std::size_t n) noexcept {
        config_.numSources = std::clamp(n, std::size_t{1}, kMaxSources);
        refreshGates();
    }

    /// @brief Select a slot's model (FR-011). Out-of-range slot: silent no-op.
    ///
    /// The change is DUCKED, not applied on the spot (FR-013): the write arms the
    /// FSM, the gate ramps to zero, the swap happens on the exact zero sample and
    /// the gate ramps back. getSourceModel() therefore reports the OLD value for
    /// up to kGainRampMs * 0.5 after the call - it is an applied-state getter, and
    /// reporting the pending value would make it disagree with the audio.
    void setSourceModel(std::size_t slot, NoiseOrganismModel model) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        const Slot& s = slots_[slot];
        // The requested TYPE half of the goal is carried through unchanged, so a
        // model change never forgets the type FR-012 has to restore.
        requestSourceState(slot, model, s.duckPending ? s.pendingType : s.requestedType);
    }

    /// @brief Select a Direct slot's noise type (FR-012).
    /// NoiseType::ModulationNoise is not selectable - it is floor-less
    /// (noise_generator.h:553-558) and would render dead under the zero sidechain
    /// - and is snapped to TapeHiss. The requested value is remembered across
    /// model changes; getSourceNoiseType() reports the EFFECTIVE type.
    void setSourceNoiseType(std::size_t slot, NoiseType type) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        const Slot& s = slots_[slot];
        requestSourceState(slot, s.duckPending ? s.pendingModel : s.model, type);
    }

    /// @brief Slot mix level in dB, clamped to NoiseGenerator's own
    /// [kMinLevelDb, kMaxLevelDb] = [-96, +12] (noise_generator.h:104-105).
    void setSourceLevel(std::size_t slot, float dB) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s   = slots_[slot];
        s.levelDb = std::clamp(sanitise(dB, -12.0f), -96.0f, 12.0f);
        // A 0-100 % ramp of exactly kGainRampMs whatever the step size: LinearRamp
        // recomputes increment = delta / (rampTimeMs * 0.001 * sampleRate) on every
        // setTarget (smoother.h:100-108, :342-354), so this is constant-DURATION
        // despite the class doc's "constant rate" wording (FR-073, SC-009 (a)).
        //
        // The duck is deliberately NOT armed: a level change touches no generator
        // state, so there is nothing to duck around. generator.setNoiseLevel is
        // never called from here either - the generator carries only the FR-017
        // per-type calibration constant and levelRamp is the SOLE owner of the
        // user's slot level (plan S9.1). Writing the level into the generator too
        // would leave it stale until the next duck, which is an unducked step the
        // moment one arrives.
        s.levelRamp.configure(kGainRampMs, static_cast<float>(sampleRate_));
        s.levelRamp.setTarget(dbToGain(s.levelDb));
    }

    // =========================================================================
    // Per-slot chain (FR-051 .. FR-057)
    // =========================================================================

    /// @brief Enabled resonators in a slot, clamped [0, kMaxResonatorsPerSource].
    /// At 0 the resonator stage is SKIPPED, not bypassed: with nothing enabled
    /// ResonatorBank::process returns silence, not the dry input
    /// (resonator_bank.h:511 with exciterMix_ == 0 at :589).
    void setNumResonators(std::size_t slot, std::size_t n) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].numResonators = std::min(n, kMaxResonatorsPerSource);
        applySlotConfiguration(slot);
    }

    /// @brief Anchor frequency the resonator's wander lane orbits (FR-052).
    /// Clamped to [kMinResonatorFrequency, sampleRate * kMaxResonatorFrequencyRatio]
    /// (resonator_bank.h:42, :45).
    void setResonatorAnchor(std::size_t slot, std::size_t index, float hz) noexcept {
        if (!validSlot(slot) || index >= kMaxResonatorsPerSource) {
            return;
        }
        slots_[slot].anchorHz[index] =
            std::clamp(sanitise(hz, kDefaultAnchorHz[index]),
                       kMinResonatorFrequency, maxResonatorHz());
        applySlotConfiguration(slot);
    }

    /// @brief Nominal RT60 at the anchor, clamped [kMinDecayTime, kMaxDecayTime]
    /// = [0.001, 30] s (resonator_bank.h:54, :57). Applies to every enabled
    /// resonator in the slot.
    void setResonatorDecay(std::size_t slot, float seconds) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].decaySeconds =
            std::clamp(sanitise(seconds, 1.5f), kMinDecayTime, kMaxDecayTime);
        applySlotConfiguration(slot);
    }

    /// @brief Enabled combs in a slot, clamped [0, kMaxCombsPerSource].
    /// At 0 the comb stage is SKIPPED: TimeVaryingCombBank::setNumCombs floors at
    /// 1 (timevar_comb_bank.h:502), so forwarding 0 would leave one comb running.
    void setNumCombs(std::size_t slot, std::size_t n) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].numCombs = std::min(n, kMaxCombsPerSource);
        applySlotConfiguration(slot);
    }

    /// @brief Comb ratio law inputs (FR-042, FR-057): the organism evaluates
    /// f[n] = fundamentalHz * sqrt(1 + n * spread) itself rather than relying on
    /// Tuning::Inharmonic, because writing setCombDelay puts the bank in
    /// Tuning::Custom unconditionally (timevar_comb_bank.h:515).
    /// The two arguments are sanitised INDEPENDENTLY, so a finite spread survives
    /// a non-finite fundamental.
    void setCombTuning(std::size_t slot, float fundamentalHz, float spread) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s = slots_[slot];
        s.combFundamental = std::clamp(sanitise(fundamentalHz, 60.0f),
                                       kMinResonatorFrequency, maxResonatorHz());
        s.combSpread = std::clamp(sanitise(spread, 0.35f), 0.0f, 1.0f);
        applySlotConfiguration(slot);
    }

    /// @brief Comb feedback, clamped [0, kCombFeedbackCap] - FR-090's stability
    /// cap, deliberately below TimeVaryingCombBank's own higher limit.
    ///
    /// Writing this LATCHES the slot's feedback to the caller's value: from here
    /// on the per-model default (FR-042: kMetallicCombFeedback on MetallicHiss,
    /// kDefaultCombFeedback elsewhere) no longer applies to this slot, in either
    /// direction, and a later model change leaves the value alone. Only
    /// prepare() clears the latch, because prepare() is the single documented
    /// path back to the FR-016 defaults (FR-002).
    void setCombFeedback(std::size_t slot, float feedback) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s = slots_[slot];
        s.combFeedback =
            std::clamp(sanitise(feedback, kDefaultCombFeedback), 0.0f, kCombFeedbackCap);
        s.combFeedbackUserSet = true;
        applySlotConfiguration(slot);
    }

    /// @brief Chain-filter base cutoff (FR-056), clamped [20 Hz, 0.45 * sampleRate].
    void setFilterBaseCutoff(std::size_t slot, float hz) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].filterBaseCutoffHz =
            std::clamp(sanitise(hz, 800.0f), 20.0f, maxFilterHz());
        applySlotConfiguration(slot);
    }

    /// @brief Chain-filter base resonance (FR-056), clamped to the SVF's legal Q
    /// range [SVF::kMinQ, SVF::kMaxQ] (svf.h:120, :123).
    void setFilterBaseResonance(std::size_t slot, float q) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].filterBaseQ =
            std::clamp(sanitise(q, 0.7f), SVF::kMinQ, SVF::kMaxQ);
        applySlotConfiguration(slot);
    }

    // =========================================================================
    // Composed models (FR-032, FR-035, FR-041)
    // =========================================================================

    /// @brief Granular-dust carrier colour (FR-032). Seven of eight colours are
    /// selectable; NoiseColor::Velvet is rejected and snapped to the Brown
    /// default on a musical-design ground - Velvet is the sparse impulsive
    /// TRIGGER of this model, not a continuous carrier.
    void setDustCarrierColor(std::size_t slot, NoiseColor c) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].dustColor = (c == NoiseColor::Velvet) ? NoiseColor::Brown : c;
        applySlotConfiguration(slot);
    }

    /// @brief Requested grain length in ms, clamped [5, 200] and then capped by
    /// the FR-035 mean-concurrency ceiling. getDustGrainMs() reports the
    /// EFFECTIVE value so the cap is observable, not silent.
    void setDustGrainMs(std::size_t slot, float ms) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].dustGrainMsRequested = sanitise(ms, 40.0f);
        updateDustEffective(slots_[slot]);
        // The grain length itself never reaches a sub-component - it is the
        // organism's own phase increment - but the FR-035 rule is BIDIRECTIONAL,
        // so a grain-length write can move the effective density's companion
        // state. Routed through the single owner for the same reason every other
        // setter is: two copies of the push expression are how they drift apart.
        applySlotConfiguration(slot);
    }

    /// @brief Requested impulse density, clamped [100, 20000] - the range
    /// NoiseGenerator::setVelvetDensity itself enforces (noise_generator.h:315-317).
    /// Note the floor of 100: the concurrency rule cannot be met by lowering
    /// density alone, which is why grain length is capped second (FR-035).
    void setDustDensity(std::size_t slot, float impulsesPerSecond) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].dustDensityRequested = sanitise(impulsesPerSecond, 100.0f);
        updateDustEffective(slots_[slot]);
        // Where the effective density actually reaches the velvet trigger train
        // (applySlotConfiguration -> generator.setVelvetDensity).
        applySlotConfiguration(slot);
    }

    /// @brief MetallicHiss base type: Blue (false) or Violet (true) - FR-041.
    void setHissBright(std::size_t slot, bool violet) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s = slots_[slot];
        s.hissViolet = violet;
        s.activeType = effectiveNoiseType(s.model, s.requestedType, s.hissViolet);
        applySlotConfiguration(slot);
    }

    // =========================================================================
    // Wander lanes (FR-061 .. FR-069)
    // =========================================================================

    /// @brief Resonator-frequency wander span in semitones, clamped [0, 12]
    /// (FR-061), plus this slot's Brownian smoothness in SECONDS.
    /// A span of 0 freezes the parameter but the lane keeps advancing (FR-066).
    void setResonatorWander(std::size_t slot, float semitones,
                            float smoothnessSeconds) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s = slots_[slot];
        s.resWanderSemis = std::clamp(sanitise(semitones, 2.0f), 0.0f, 12.0f);
        const float normalised = smoothnessSecondsToNormalised(
            sanitise(smoothnessSeconds, kDefaultSmoothnessSeconds));
        for (BrownianDrift& lane : s.resFreqLane) {
            lane.setSmoothness(normalised);
        }
    }

    /// @brief Resonator-Q wander depth, clamped [0, 1] (FR-064). Shares the
    /// frequency lane; the mapping is strictly downward, because rt60ToQ is
    /// already saturated at kMaxResonatorQ for drone-scale decays.
    void setResonatorQWander(std::size_t slot, float amount) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].resQWander = std::clamp(sanitise(amount, 0.25f), 0.0f, 1.0f);
    }

    /// @brief Chain-filter cutoff wander span in octaves, clamped [0, 6]
    /// (FR-062), plus this slot's Brownian smoothness in SECONDS.
    void setFilterWander(std::size_t slot, float octaves,
                         float smoothnessSeconds) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s = slots_[slot];
        s.cutoffWanderOct = std::clamp(sanitise(octaves, 1.5f), 0.0f, 6.0f);
        s.cutoffLane.setSmoothness(smoothnessSecondsToNormalised(
            sanitise(smoothnessSeconds, kDefaultSmoothnessSeconds)));
    }

    /// @brief Chain-filter resonance wander depth, clamped [0, 1] (FR-067), plus
    /// this slot's Brownian smoothness in SECONDS. This lane exists because the
    /// roadmap's Phase-2 core requirement is "all filter parameters wander" and
    /// resonance is otherwise the one named parameter that never moves.
    void setFilterResonanceWander(std::size_t slot, float amount,
                                  float smoothnessSeconds) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s = slots_[slot];
        s.resonanceWander = std::clamp(sanitise(amount, 0.2f), 0.0f, 1.0f);
        s.resonanceLane.setSmoothness(smoothnessSecondsToNormalised(
            sanitise(smoothnessSeconds, kDefaultSmoothnessSeconds)));
    }

    /// @brief Comb-delay wander span in percent, clamped [0, 50] (FR-063), plus
    /// the Perlin lane rate in cells/s (PerlinNoiseSource clamps it to
    /// [kMinRate, kMaxRate] = [0.005, 5], perlin_noise_source.h:177-179).
    /// No extra slew limiter is needed here: TimeVaryingCombBank already smooths
    /// delay changes over kDelaySmoothingMs = 20 ms (timevar_comb_bank.h:109).
    void setCombWander(std::size_t slot, float percent, float ratePerSecond) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s = slots_[slot];
        s.combWanderPct = std::clamp(sanitise(percent, 12.0f), 0.0f, 50.0f);
        const float rate = sanitise(ratePerSecond, kDefaultWanderRateHz);
        for (PerlinNoiseSource& lane : s.combLane) {
            lane.setRate(rate);
        }
    }

    /// @brief Organism-wide wander master switch (FR-068), default true.
    /// Both halves matter: it zeroes every external lane's contribution AND
    /// calls setCutoffRandomEnabled(false) on every slot's StochasticFilter,
    /// whose internal randomiser defaults to ON (stochastic_filter.h:555) at
    /// kDefaultChangeRate = 1 Hz over 2 octaves (:103, :112). It deliberately
    /// does NOT touch breathing - FR-068 enumerates FR-061..FR-067 only.
    void setWanderEnabled(bool enabled) noexcept {
        wanderEnabled_ = enabled;
        // Half two of the switch. wanderScale() alone only zeroes the EXTERNAL
        // lane spans; the chain filter carries its own randomiser, so without
        // this forward a "wander off" organism still wanders spectrally.
        // Forwarded unconditionally, prepared or not: StochasticFilter's setter
        // only writes a flag (stochastic_filter.h:311), and applySlotConfiguration
        // re-pushes wanderEnabled_ on every later configuration change, so the two
        // paths cannot disagree.
        for (Slot& s : slots_) {
            s.filter.setCutoffRandomEnabled(enabled);
        }
        // The lanes themselves are deliberately NOT stopped - they keep
        // advancing, so re-enabling never jumps (FR-066, plan S7.3).
    }

    /// @brief The single organism-wide wander-rate scalar (FR-069), clamped to
    /// StochasticFilter's [kMinChangeRate, kMaxChangeRate] = [0.01, 100] Hz
    /// (stochastic_filter.h:101-102). Each lane kind converts it into its own
    /// domain; see smoothnessSecondsToNormalised() for the Brownian half.
    void setWanderRate(float hz) noexcept {
        wanderRateHz_ = std::clamp(sanitise(hz, kDefaultWanderRateHz),
                                   StochasticFilter::kMinChangeRate,
                                   StochasticFilter::kMaxChangeRate);
        // ONE scalar, four lane domains (plan S7.2). The Brownian half goes
        // through the seconds -> NORMALISED conversion, never a raw forward:
        // BrownianDrift::setSmoothness takes a normalised [0,1] argument
        // (brownian_drift.h:149-153), so forwarding a tau in seconds would clamp
        // to s = 1 for every hz <= 1 and would mean tau = 6.2 s for a requested
        // 0.2 s at hz = 5.
        const float laneSmoothness =
            smoothnessSecondsToNormalised(1.0f / wanderRateHz_);
        for (Slot& s : slots_) {
            for (BrownianDrift& lane : s.resFreqLane) {
                lane.setSmoothness(laneSmoothness);
            }
            s.cutoffLane.setSmoothness(laneSmoothness);
            s.resonanceLane.setSmoothness(laneSmoothness);
            for (PerlinNoiseSource& lane : s.combLane) {
                lane.setRate(wanderRateHz_);  // clamps [0.005, 5] itself
            }
            s.breathing.setRate(wanderRateHz_);  // clamps [0.01, 0.5] itself
            s.filter.setChangeRate(wanderRateHz_);
        }
    }

    // =========================================================================
    // Breathing and event hooks (FR-070 .. FR-073)
    // =========================================================================

    /// @brief Per-slot level breathing (FR-070). `rateHz` is forwarded to
    /// BreathingModulator::setRate ([0.01, 0.5] Hz, breathing_modulator.h:108-110)
    /// and `irregularity` to setIrregularity. `depth` is clamped [0, 1] and owned
    /// by the organism's affine gain map - it is deliberately NOT forwarded to
    /// BreathingModulator::setDepth, which stays at its library default 1.0
    /// (breathing_modulator.h:112); forwarding both would square it.
    void setSourceBreathing(std::size_t slot, float rateHz, float depth,
                            float irregularity) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        Slot& s = slots_[slot];
        s.breathDepth = std::clamp(sanitise(depth, 0.25f), 0.0f, 1.0f);
        s.breathIrregularity = std::clamp(sanitise(irregularity, 0.3f), 0.0f, 1.0f);
        s.breathing.setRate(sanitise(rateHz, kDefaultWanderRateHz));
        s.breathing.setIrregularity(s.breathIrregularity);
    }

    /// @brief Dormancy (FR-071). A dormant slot contributes exactly zero, and
    /// skips only the resonator/comb/filter stages: its source keeps rendering
    /// and its lanes keep advancing, so waking reveals neither a rewound noise
    /// stream nor stale colour-filter state.
    void setSourceDormant(std::size_t slot, bool dormant) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].dormant = dormant;
        refreshGates();
    }

    /// @brief Wake amount (FR-072), clamped [0, 1]. A plain scalar input, not a
    /// scheduler reference: Vorago Phase 10 owns the SlowEventScheduler and
    /// writes its envelope value here.
    void setSourceWake(std::size_t slot, float amount) noexcept {
        if (!validSlot(slot)) {
            return;
        }
        slots_[slot].wakeAmount = std::clamp(sanitise(amount, 1.0f), 0.0f, 1.0f);
        refreshGates();
    }

    // =========================================================================
    // Read surface (FR-015)
    // =========================================================================
    // Out-of-range slot/index returns the documented neutral and never reads out
    // of bounds: 0.0f for float getters, 0 for size getters, Direct, Brown
    // (NoiseType and NoiseColor alike), false.
    // =========================================================================

    [[nodiscard]] std::size_t getNumSources() const noexcept { return config_.numSources; }

    [[nodiscard]] NoiseOrganismModel getSourceModel(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].model : NoiseOrganismModel::Direct;
    }

    /// @brief The EFFECTIVE noise type after FR-012's ModulationNoise rejection
    /// and each composed model's base-type pinning.
    [[nodiscard]] NoiseType getSourceNoiseType(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].activeType : NoiseType::Brown;
    }

    [[nodiscard]] float getSourceLevel(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].levelDb : 0.0f;
    }

    [[nodiscard]] bool isSourceDormant(std::size_t slot) const noexcept {
        return validSlot(slot) && slots_[slot].dormant;
    }

    [[nodiscard]] float getSourceWakeAmount(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].wakeAmount : 0.0f;
    }

    [[nodiscard]] std::size_t getNumResonators(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].numResonators : std::size_t{0};
    }

    [[nodiscard]] std::size_t getNumCombs(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].numCombs : std::size_t{0};
    }

    [[nodiscard]] float getCombFundamental(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].combFundamental : 0.0f;
    }

    [[nodiscard]] float getCombSpread(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].combSpread : 0.0f;
    }

    /// @brief The EFFECTIVE comb feedback - the per-model default (FR-042) until
    /// a caller writes one, that caller's value afterwards. Reported effective
    /// rather than as-requested for the same reason getDustGrainMs is (FR-015):
    /// the value that reaches the audio must be readable.
    [[nodiscard]] float getCombFeedback(std::size_t slot) const noexcept {
        return validSlot(slot) ? effectiveCombFeedback(slots_[slot]) : 0.0f;
    }

    /// @brief EFFECTIVE density after the FR-035 clamp.
    [[nodiscard]] float getDustDensity(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].dustDensityEffective : 0.0f;
    }

    /// @brief EFFECTIVE grain length after the FR-035 concurrency ceiling.
    [[nodiscard]] float getDustGrainMs(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].dustGrainMsEffective : 0.0f;
    }

    /// @brief EFFECTIVE carrier colour after the FR-032 Velvet rejection.
    [[nodiscard]] NoiseColor getDustCarrierColor(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].dustColor : NoiseColor::Brown;
    }

    [[nodiscard]] bool isWanderEnabled() const noexcept { return wanderEnabled_; }

    [[nodiscard]] float getWanderRate() const noexcept { return wanderRateHz_; }

    // ---- applied-state echo --------------------------------------------------
    // These exist because the success criteria need a deterministic, noise-free
    // observable and the audio signal is not one.

    /// @brief level x breath x gate, as applied to the mix this sample (FR-050).
    [[nodiscard]] float getSourceGain(std::size_t slot) const noexcept {
        if (!validSlot(slot)) {
            return 0.0f;
        }
        const Slot& s = slots_[slot];
        return s.levelRamp.getCurrentValue() * s.breathGain * s.gate.getCurrentValue();
    }

    [[nodiscard]] float getResonatorCurrentFrequency(std::size_t slot,
                                                     std::size_t index) const noexcept {
        if (!validSlot(slot) || index >= kMaxResonatorsPerSource) {
            return 0.0f;
        }
        return slots_[slot].appliedResHz[index];
    }

    [[nodiscard]] float getResonatorCurrentQ(std::size_t slot,
                                             std::size_t index) const noexcept {
        if (!validSlot(slot) || index >= kMaxResonatorsPerSource) {
            return 0.0f;
        }
        return slots_[slot].appliedResQ[index];
    }

    [[nodiscard]] float getFilterCurrentCutoff(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].appliedCutoffHz : 0.0f;
    }

    [[nodiscard]] float getCombCurrentDelayMs(std::size_t slot,
                                              std::size_t index) const noexcept {
        if (!validSlot(slot) || index >= kMaxCombsPerSource) {
            return 0.0f;
        }
        return slots_[slot].appliedCombDelayMs[index];
    }

    /// @brief LINEAR RMS of the SOURCE stage, pre-chain (plan S5.6).
    ///
    /// Linear amplitude, not dBFS - a deliberate departure from plan S5.6's
    /// "returned in dBFS" wording. FR-015's documented neutral for every float
    /// getter is `0.0f`, and `0.0f` is only coherent as *silence* in the linear
    /// domain; in dBFS it would read as FULL SCALE, both out of range and on a
    /// freshly reset instance. Callers that want dB apply gainToDb()
    /// (core/db_utils.h:317), one call at the call site rather than one log10
    /// per slot per control step in here.
    ///
    /// Measured on the SOURCE stage (post-source, pre-resonator, pre-gain)
    /// because that is the only reading under which SC-010 (a)'s
    /// dormant-then-woken vs always-awake agreement is satisfiable: a dormant
    /// slot has no chain output at all (FR-071), and a just-woken one carries a
    /// settling chain for seconds (SC-010 (b)).
    ///
    /// Accumulated per slot inside renderChunk() on that slot's OWN scratch data
    /// and folded by a one-pole in updateControl() at the next control-step
    /// boundary - never read out of the shared scratch buffer in updateControl(),
    /// which at that instant still holds the PREVIOUS chunk's LAST slot (S5.6).
    [[nodiscard]] float getSourceRms(std::size_t slot) const noexcept {
        return validSlot(slot) ? slots_[slot].sourceRmsSmoothed : 0.0f;
    }

    [[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept {
        return clampEngagements_;
    }

    /// @brief Bytes requested at prepare() time. Self-reported because
    /// AllocationDetector has no byte accounting - its operator-new replacements
    /// discard `size` (tests/test_helpers/allocation_detector.h:83-89).
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return allocatedBytes_; }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // =========================================================================
    // Compile-time salt table (FR-005)
    // =========================================================================
    // APPEND ONLY: a later phase adding a lane must take a NEW base. Renumbering
    // any base silently changes every Phase-2 render, because each lane's stream
    // is deriveStreamSeed(seed_, base + offset) (core/random.h:102).
    //
    // TimeVaryingCombBank takes NO salt: it exposes no setSeed and hard-seeds
    // ch.rng.seed(12345u + i * 7919u) (timevar_comb_bank.h:466, repeated at :487),
    // so its internal motion would be bit-identical across slots. FR-042
    // therefore pins setModDepth/setRandomModulation at their 0.0f library
    // defaults (:414, :416) and ALL comb motion comes from the salted Perlin
    // lanes below.
    // =========================================================================
    static constexpr std::size_t kSaltNoiseGen      = 0;    // + slot
    static constexpr std::size_t kSaltDustCarrier   = 16;   // + slot
    static constexpr std::size_t kSaltChainFilter   = 32;   // + slot
    static constexpr std::size_t kSaltResonatorLane = 48;   // + slot*kMaxResonatorsPerSource + index
    static constexpr std::size_t kSaltFilterCutoff  = 64;   // + slot
    static constexpr std::size_t kSaltFilterReso    = 80;   // + slot
    static constexpr std::size_t kSaltCombLane      = 96;   // + slot*kMaxCombsPerSource + index
    static constexpr std::size_t kSaltBreathing     = 112;  // + slot
    static constexpr std::size_t kSaltNextFree      = 128;

    static_assert(kSaltNoiseGen + kMaxSources <= kSaltDustCarrier,
                  "noise-generator salts must not overlap the dust-carrier block");
    static_assert(kSaltDustCarrier + kMaxSources <= kSaltChainFilter,
                  "dust-carrier salts must not overlap the chain-filter block");
    static_assert(kSaltChainFilter + kMaxSources <= kSaltResonatorLane,
                  "chain-filter salts must not overlap the resonator-lane block");
    static_assert(kSaltResonatorLane + kMaxSources * kMaxResonatorsPerSource <= kSaltFilterCutoff,
                  "resonator lane salts must not overlap the cutoff block");
    static_assert(kSaltFilterCutoff + kMaxSources <= kSaltFilterReso,
                  "filter-cutoff salts must not overlap the filter-resonance block");
    static_assert(kSaltFilterReso + kMaxSources <= kSaltCombLane,
                  "filter-resonance salts must not overlap the comb-lane block");
    static_assert(kSaltCombLane + kMaxSources * kMaxCombsPerSource <= kSaltBreathing,
                  "comb lane salts must not overlap the breathing block");
    static_assert(kSaltBreathing + kMaxSources <= kSaltNextFree, "salt table overflow");

    // =========================================================================
    // Non-finite argument contract (FR-008) - NORMATIVE
    // =========================================================================
    // std::clamp does NOT reject NaN: with v = NaN both `v < lo` and `hi < v` are
    // false, so v is returned unchanged. A clamp-only setter therefore admits NaN
    // into configuration state, and the trace is fatal:
    //   setResonatorAnchor(slot, i, NaN) -> anchorHz[i] = NaN
    //     -> driftedHz = clampFreq(NaN * exp2(..)) = NaN
    //     -> ResonatorBank::setFrequency clamps with a bare std::clamp (:539)
    //     -> BiquadCoefficients::calculate clamps with another (biquad.h:155)
    //     -> NaN omega -> NaN coefficients. Biquad::process resets only on a
    //        non-finite INPUT SAMPLE (biquad.h:354), never on non-finite
    //        coefficients, so the resonator emits NaN forever - and FR-074's
    //        output clamp is itself a std::clamp and propagates it.
    // The identical path exists for setFilterBaseCutoff -> SVF::setCutoff ->
    // std::tan(NaN), and for setCombTuning -> setCombDelay
    // (timevar_comb_bank.h:514).
    //
    // BLANKET RULE: every float-taking public setter calls sanitise() as its
    // FIRST statement, BEFORE any clamp. The substituted value is then clamped
    // and stored exactly as a legal write would be, so it is observable through
    // the FR-015 read surface and can never leave a half-applied state.
    // std::size_t, bool and enum arguments cannot be non-finite and take no guard.
    //
    //   | Setter                    | Argument(s)                  | Neutral                              |
    //   |---------------------------|------------------------------|--------------------------------------|
    //   | setSourceLevel            | dB                           | -12.0f                               |
    //   | setResonatorAnchor        | hz                           | kDefaultAnchorHz[index] (70/140/260/500) |
    //   | setResonatorDecay         | seconds                      | 1.5f                                 |
    //   | setCombTuning             | fundamentalHz, spread        | 60.0f, 0.35f - guarded INDEPENDENTLY |
    //   | setCombFeedback           | feedback                     | kDefaultCombFeedback (0.55f)         |
    //   | setFilterBaseCutoff       | hz                           | 800.0f                               |
    //   | setFilterBaseResonance    | q                            | 0.7f                                 |
    //   | setDustGrainMs            | ms                           | 40.0f                                |
    //   | setDustDensity            | impulsesPerSecond            | 100.0f                               |
    //   | setResonatorWander        | semitones, smoothnessSeconds | 2.0f, 1/kDefaultWanderRateHz         |
    //   | setResonatorQWander       | amount                       | 0.25f                                |
    //   | setFilterWander           | octaves, smoothnessSeconds   | 1.5f, 1/kDefaultWanderRateHz         |
    //   | setFilterResonanceWander  | amount, smoothnessSeconds    | 0.2f, 1/kDefaultWanderRateHz         |
    //   | setCombWander             | percent, ratePerSecond       | 12.0f, kDefaultWanderRateHz          |
    //   | setWanderRate             | hz                           | kDefaultWanderRateHz                 |
    //   | setSourceBreathing        | rateHz, depth, irregularity  | kDefaultWanderRateHz, 0.25f, 0.3f    |
    //   | setSourceWake             | amount                       | 1.0f                                 |
    //   | prepare                   | sampleRate (double)          | 48000.0 (double overload)            |
    //   | PrepareConfig             | maxCombDelayMs               | 50.0f                                |
    //
    // Derived values are guarded too, not just the setters: every value written
    // into a chained component on the control grid passes through
    // sanitise(value, <that stage's base value>) immediately before the call, in
    // addition to its clamp.
    // =========================================================================

    /// @brief Returns `v` when finite, otherwise the documented neutral.
    /// detail::isFinite is the fast-math-immune exponent test (core/db_utils.h:118)
    /// - a plain `v != v` or std::isnan folds away on the macOS -ffast-math leg.
    [[nodiscard]] static constexpr float sanitise(float v, float neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }

    /// @brief Double overload, for prepare()'s sampleRate (db_utils.h:125).
    [[nodiscard]] static constexpr double sanitise(double v, double neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }

    /// FR-016 resonator anchors, also the per-index sanitise neutrals.
    static constexpr std::array<float, kMaxResonatorsPerSource> kDefaultAnchorHz{
        70.0f, 140.0f, 260.0f, 500.0f};

    /// The seconds-domain neutral for every Brownian smoothness argument.
    static constexpr float kDefaultSmoothnessSeconds = 1.0f / kDefaultWanderRateHz;

    // =========================================================================
    // Per-slot state (plan S1.3)
    // =========================================================================

    /// Duck-and-restore state for the FR-013 type/model change gate.
    enum class Duck : std::uint8_t { Idle, Down, Up };

    struct Slot {
        // ---- sources ----
        NoiseGenerator  generator;  ///< Trigger for GranularDust, audio otherwise.
        NoiseOscillator carrier;    ///< Dust carrier only.
        // ---- chain ----
        ResonatorBank       resonators;
        TimeVaryingCombBank combs;
        StochasticFilter    filter;
        // ---- wander lanes (all salted, FR-005) ----
        std::array<BrownianDrift, kMaxResonatorsPerSource> resFreqLane;  ///< Freq AND Q (FR-064).
        BrownianDrift                                      cutoffLane;
        BrownianDrift                                      resonanceLane;
        std::array<PerlinNoiseSource, kMaxCombsPerSource>   combLane;
        BreathingModulator                                 breathing;
        // ---- configuration (organism-owned, survives reset()) ----
        NoiseOrganismModel model         = NoiseOrganismModel::Direct;
        NoiseType          requestedType = NoiseType::Brown;  ///< Remembered across model changes.
        NoiseType          activeType    = NoiseType::Brown;  ///< Effective type (FR-012).
        float              levelDb       = -12.0f;
        std::size_t        numResonators = 2;
        std::size_t        numCombs      = 2;
        std::array<float, kMaxResonatorsPerSource> anchorHz{70.0f, 140.0f, 260.0f, 500.0f};
        float decaySeconds    = 1.5f;
        float combFundamental = 60.0f;
        float combSpread      = 0.35f;
        float combFeedback    = kDefaultCombFeedback;
        /// FR-042's per-model feedback default applies only while this is false.
        /// setCombFeedback latches it; only prepare() clears it (FR-002).
        bool  combFeedbackUserSet = false;
        std::array<float, kMaxCombsPerSource> combBaseDelayMs{};     ///< Derived from the ratio law.
        std::array<float, kMaxCombsPerSource> appliedCombDelayMs{};  ///< Last value pushed to the
                                                                     ///< bank: the slew limiter's
                                                                     ///< previous state AND the
                                                                     ///< getCombCurrentDelayMs echo.
        float filterBaseCutoffHz = 800.0f;
        float filterBaseQ        = 0.7f;
        float resWanderSemis     = 2.0f;
        float resQWander         = 0.25f;
        float cutoffWanderOct    = 1.5f;
        float resonanceWander    = 0.2f;
        float combWanderPct      = 12.0f;
        float breathDepth        = 0.25f;
        float breathIrregularity = 0.3f;
        float dustGrainMsRequested  = 40.0f;
        float dustDensityRequested  = 100.0f;
        float dustGrainMsEffective  = 40.0f;
        float dustDensityEffective  = 100.0f;
        float dustGrainGain         = 1.0f;   ///< FR-036; recomputed on density/length change.
        NoiseColor dustColor  = NoiseColor::Brown;
        bool       hissViolet = false;
        bool       dormant    = false;
        float      wakeAmount = 1.0f;
        // ---- applied state (control-grid outputs, read by the FR-015 echo) ----
        std::array<float, kMaxResonatorsPerSource> appliedResHz{};
        std::array<float, kMaxResonatorsPerSource> appliedResQ{};
        float appliedCutoffHz    = 800.0f;
        float breathGain         = 1.0f;  ///< FR-070 affine map, per control step.
        float sourceRmsSmoothed  = 0.0f;  ///< LINEAR, one-pole; see getSourceRms.
        /// Sum of squares of THIS slot's source stage over the control period in
        /// progress, accumulated in renderChunk() and folded (then cleared) by
        /// foldSourceRms() at the next control-step boundary. Two consecutive
        /// control steps always bracket exactly kControlChunkSamples samples
        /// however the caller partitions its blocks, so the folded trajectory is
        /// block-size invariant like the audio (SC-016).
        float       sourceSumSq    = 0.0f;
        std::size_t sourceSumCount = 0;
        // ---- ramps (per-sample, linear in gain - FR-073) ----
        LinearRamp levelRamp;  ///< -> dbToGain(levelDb)
        LinearRamp gate;       ///< -> dormant/dropped/wake/duck target
        // ---- duck FSM (FR-013) ----
        Duck               duckState   = Duck::Idle;
        bool               duckPending = false;
        NoiseOrganismModel pendingModel = NoiseOrganismModel::Direct;
        NoiseType          pendingType  = NoiseType::Brown;
        // ---- dust pool ----
        std::array<DustGrain, kMaxDustGrains> grains{};
        /// Where acquireGrain() starts its FREE-slot scan, so the pool is used
        /// round-robin. It is NOT a steal cursor: when every grain is live the
        /// victim is chosen by largest phase, not by this index.
        std::size_t                           grainCursor = 0;
    };

    // =========================================================================
    // Private helpers
    // =========================================================================

    [[nodiscard]] static constexpr bool validSlot(std::size_t slot) noexcept {
        return slot < kMaxSources;
    }

    /// Upper bound for any resonator/comb-fundamental frequency at this rate
    /// (resonator_bank.h:42, :45).
    [[nodiscard]] float maxResonatorHz() const noexcept {
        return static_cast<float>(sampleRate_) * kMaxResonatorFrequencyRatio;
    }

    /// Upper bound for the chain filter's cutoff (FR-062).
    [[nodiscard]] float maxFilterHz() const noexcept {
        return 0.45f * static_cast<float>(sampleRate_);
    }

    /// @brief The EFFECTIVE base type: FR-012's ModulationNoise rejection for
    /// Direct slots, and each composed model's pinned base type (FR-021 Brown,
    /// FR-031 Velvet-as-trigger, FR-041 Blue/Violet).
    [[nodiscard]] static constexpr NoiseType effectiveNoiseType(
        NoiseOrganismModel model, NoiseType requested, bool hissViolet) noexcept {
        switch (model) {
        case NoiseOrganismModel::FilteredWind:
            return NoiseType::Brown;
        case NoiseOrganismModel::GranularDust:
            return NoiseType::Velvet;
        case NoiseOrganismModel::MetallicHiss:
            return hissViolet ? NoiseType::Violet : NoiseType::Blue;
        case NoiseOrganismModel::Direct:
        default:
            break;
        }
        return (requested == NoiseType::ModulationNoise) ? NoiseType::TapeHiss : requested;
    }

    /// @brief The comb feedback a slot runs at when no caller has written one
    /// (FR-016's table, FR-042's derivation): MetallicHiss rings hotter because
    /// the inharmonic ring is the model's whole point, every other model takes
    /// the general default. Both are inside kCombFeedbackCap by static_assert.
    [[nodiscard]] static constexpr float defaultCombFeedback(
        NoiseOrganismModel model) noexcept {
        return (model == NoiseOrganismModel::MetallicHiss) ? kMetallicCombFeedback
                                                           : kDefaultCombFeedback;
    }

    /// @brief The comb feedback actually pushed to the bank, and the value
    /// getCombFeedback() reports. The single evaluation point of the
    /// default-vs-latched rule, so the read surface and the audio cannot
    /// disagree.
    ///
    /// Deliberately NOT constexpr: Slot is not a literal type (it owns a
    /// NoiseGenerator, a ResonatorBank and eleven modulation lanes), so a
    /// constexpr function taking one could never be evaluated in a constant
    /// expression - ill-formed, no diagnostic required.
    [[nodiscard]] static float effectiveCombFeedback(const Slot& s) noexcept {
        return s.combFeedbackUserSet ? s.combFeedback : defaultCombFeedback(s.model);
    }

    /// @brief FR-035's bidirectional mean-concurrency rule, density first,
    /// grain length second. At the 100 imp/s floor the ceiling is 240 ms.
    ///
    /// The order is forced, not stylistic: the clamp has to land on the GRAIN
    /// LENGTH because NoiseGenerator::setVelvetDensity floors at 100 itself
    /// (noise_generator.h:340), so a density below that is simply not reachable
    /// and "lower the density instead" is not an option the component has.
    ///
    /// Also the single recompute point of the FR-036 grain gain, so the gain and
    /// the effective values can never disagree. Recomputed HERE - on a density or
    /// grain-length write - and nowhere on the render path: in-flight grains keep
    /// the gain they were born with (plan S6.1), so nothing per-sample reads it.
    static void updateDustEffective(Slot& slot) noexcept {
        slot.dustDensityEffective = std::clamp(slot.dustDensityRequested, 100.0f, 20000.0f);
        const float grainCeilingMs =
            1000.0f * static_cast<float>(kMaxDustGrains) / slot.dustDensityEffective;
        slot.dustGrainMsEffective =
            std::min(std::clamp(slot.dustGrainMsRequested, 5.0f, 200.0f), grainCeilingMs);

        // FR-036: N incoherent grains of equal amplitude sum to sqrt(N) times
        // one, so dividing by sqrt(mean concurrency) holds the model's level
        // flat across the whole density range. The floor of 1 stops a sparse
        // configuration (fewer than one grain alive on average) from being
        // AMPLIFIED - at concurrency below 1 the grains do not overlap at all
        // and there is nothing to compensate for.
        const float expectedConcurrency =
            slot.dustDensityEffective * slot.dustGrainMsEffective * 0.001f;
        slot.dustGrainGain = 1.0f / std::sqrt(std::max(1.0f, expectedConcurrency));
    }

    /// @brief Seconds -> BrownianDrift's NORMALISED smoothness argument (FR-069).
    /// BrownianDrift::setSmoothness takes a normalised [0, 1] value, not a tau in
    /// seconds (brownian_drift.h:149-153); its internal mapping is
    /// tau = kTauMin + s * (kTauMax - kTauMin). At kDefaultWanderRateHz = 0.03 the
    /// implied tau is 33.3 s, above kTauMax = 30 s, so the default sits at the
    /// clamp ceiling - a deliberate, documented consequence.
    [[nodiscard]] static float smoothnessSecondsToNormalised(float seconds) noexcept {
        const float tau =
            std::clamp(seconds, BrownianDrift::kTauMin, BrownianDrift::kTauMax);
        return std::clamp((tau - BrownianDrift::kTauMin) /
                              (BrownianDrift::kTauMax - BrownianDrift::kTauMin),
                          0.0f, 1.0f);
    }

    // =========================================================================
    // Source calibration (FR-017, plan S9.1)
    // =========================================================================
    /// The generator-side level EVERY source runs at, before its per-type drive.
    /// The user's slot level is deliberately NOT here: it is carried exclusively
    /// by the mix-stage levelRamp (plan S8.1), so the level has a single owner
    /// and a -12 dB slot cannot render at -24 dB. Chosen as the level the drive
    /// table is measured at (noise_generator.h:106), so that
    /// kSourceReferenceDb + kSourceDriveDb[White] is exactly that default.
    static constexpr float kSourceReferenceDb = NoiseGenerator::kDefaultLevelDb;  // -20 dB

    /// FR-013's forwarded floor for the two signal-dependent, FLOORED types
    /// (TapeHiss `noise_generator.h:434`, Asperity `:480`; both compute
    /// `modulation = floorGain + (1 - floorGain) * envelope * sensitivity`).
    ///
    /// FR-013 requires the organism to forward the model-specific setters "where
    /// they exist", `setTapeHissParams` (`noise_generator.h:316`) and
    /// `setAsperityParams` (`:324`) among them, and it drives every generator
    /// through `process(float*, size_t)` (`:332`) - a ZERO sidechain. With no
    /// sidechain the envelope term is identically 0 and `modulation == floorGain`
    /// for the whole render, so the floor is not a floor at all here: it is the
    /// type's entire operating point, a constant attenuation applied INSIDE the
    /// type, downstream of the level smoother.
    ///
    /// It is forwarded as 0 dB (fully open) because at the library defaults -
    /// `tapeHissFloorDb_ = -60` (`:645`), `asperityFloorDb_ = -72` (`:651`) - the
    /// attenuation is uncompensatable and SC-019 (a) is UNREACHABLE: FR-017's
    /// measured drive was +63.690 / +72.000 dB, which at kSourceReferenceDb =
    /// -20 asks `setNoiseLevel` for +43.7 / +52.0 dB against its documented
    /// `kMaxLevelDb = +12` ceiling (`:262`, `:105`). Both types were therefore
    /// clamped and rendered at -74.5 / -75 dBFS, below SC-019 (a)'s -60 dBFS
    /// non-silence floor - a selectable type that a Phase-10/12 preset could
    /// choose and get a dead slot, which is exactly the failure FR-012/SC-019
    /// exist to forbid.
    ///
    /// This is the FR-013 forward, not a work-around of the floor. FR-013's
    /// "documented, not worked around" is about the SIDECHAIN: the rejected
    /// work-around was feeding the slot's own pre-chain output back in as the
    /// sidechain (spec review note 8 - "rejected as a hidden feedback path"),
    /// and that is still not done. The floors stay exactly where they are in
    /// `NoiseGenerator`; the organism states its own operating point through the
    /// public setter FR-013 names, and FR-017's table then equalises what is
    /// left, which for TapeHiss is only its `HighShelf(5 kHz, +3 dB)`-shaped pink
    /// source (`:151`, `:427`).
    ///
    /// Consequence recorded rather than hidden: under a zero sidechain Asperity
    /// is `whiteNoise * gain * modulation` (`:483`) with `modulation == 1`, i.e.
    /// White. That is a property of the TYPE with no sidechain, not of this
    /// constant - at the -72 dB default it was White 72 dB down. If a later phase
    /// gives the organism a sidechain, THIS constant is where the modulation
    /// depth is chosen (a floor of -20 dB would give 20 dB of range), and the two
    /// drive entries must be re-measured with it.
    static constexpr float kSignalDependentFloorDb = 0.0f;

    /// The sensitivity half of the same two FR-013 forwards. Left at the
    /// library default (`noise_generator.h:646`, `:652`); with a zero sidechain
    /// it multiplies an envelope that is identically 0, so it is inert today and
    /// is stated explicitly only so the organism owns both arguments.
    static constexpr float kSignalDependentSensitivity = 1.0f;

    /// FR-017's per-NoiseType equalisation, in the NoiseType declaration order
    /// (noise_generator.h:44-58).
    ///
    /// MEASURED 2026-09-01, not authored. Method, reproducible verbatim by
    /// `NoiseOrganism_MeasureSourceDrive` (tagged `[.calibration]`, part 1, in
    /// dsp/tests/unit/systems/noise_organism_perf_test.cpp):
    ///   bare NoiseGenerator, prepare(48000.0f, 512), setSeed(1), exactly one
    ///   type enabled at NoiseGenerator::kDefaultLevelDb (-20 dB, :106), master
    ///   level left at its 0 dB default, render 60 s, take
    ///   extractAudioFeatures(render, 48000.0).rmsDbfs
    ///   (tests/test_helpers/audio_features.h:37), then
    ///   kSourceDriveDb[type] = rmsDbfs(White) - rmsDbfs(type).
    /// White measured -24.7727 dBFS and is the 0 dB reference by construction.
    ///
    /// THE WINDOW IS 60 s, NOT THE 5 s PLAN S9.1 WROTE, AND THAT IS A MEASURED
    /// DECISION. VinylCrackle fires at kDefaultCrackleDensity = 3 clicks/s
    /// (noise_generator.h:109), so a 5 s window is a 15-event sample and its RMS
    /// estimate is not stable: the same instance measures -61.478 dBFS at 5 s and
    /// -52.056 dBFS at 60 s, a 9.4 dB estimator error that alone put the type
    /// outside the generator's level range. At 60 s every type is settled - the
    /// largest 60 s -> 300 s movement across the whole roster is 0.14 dB
    /// (VinylCrackle; every other type moves under 0.05 dB) - and the impulsive
    /// types are seed-stable: VinylCrackle over seeds 1-4 spans +27.00..+27.42,
    /// Velvet +12.024..+12.041.
    ///
    /// *** THE TWO FLOORED TYPES ARE MEASURED WITH FR-013's FORWARDS APPLIED,
    /// *** AND THAT IS WHAT MAKES THEM CALIBRATABLE AT ALL. READ THIS BEFORE
    /// *** TOUCHING THE TapeHiss OR Asperity ENTRY.
    /// The generator clamps its level argument to [kMinLevelDb, kMaxLevelDb] =
    /// [-96, +12] (noise_generator.h:262, :104-105), so at
    /// kSourceReferenceDb = -20 the largest applicable drive is +32 dB.
    /// At the LIBRARY floor defaults - tapeHissFloorDb_ = -60.0f (:645),
    /// asperityFloorDb_ = -72.0f (:651) - the same procedure measured
    ///   TapeHiss  +63.690  -> applied +43.690, CLAMPED to +12, rendered -74.50 dBFS
    ///   Asperity  +72.000  -> applied +52.000, CLAMPED to +12, rendered ~-75 dBFS
    /// i.e. both BELOW SC-019 (a)'s -60 dBFS non-silence floor, because those
    /// floors are multiplicative attenuations INSIDE the type, downstream of the
    /// level smoother, that no setNoiseLevel argument can undo (Asperity's
    /// measured +72.0000 was its floor to four decimals - the type IS its floor
    /// when the sidechain is silent). That is not a calibration problem to be
    /// papered over, it is the FR-013 forward being MISSING: FR-013 requires the
    /// organism to forward setTapeHissParams / setAsperityParams, the organism
    /// now does (applySlotConfiguration), and the floor it states is
    /// kSignalDependentFloorDb - see that constant for the full argument and for
    /// why this is not the rejected sidechain work-around.
    /// With the forward in place both entries fall inside range on the same
    /// procedure, and both types clear SC-019 (a) with margin:
    ///   TapeHiss  +3.690  -> applied -16.310   (its HighShelf-shaped pink source)
    ///   Asperity  +0.000  -> applied -20.000   (White under a zero sidechain)
    /// STILL FORBIDDEN, and unchanged by the above: relaxing SC-019 (a)'s window,
    /// silently dropping a type, or feeding a synthetic sidechain. The
    /// `[.calibration]` case still REQUIREs the in-range property and remains the
    /// escalation channel plan S9.1 specifies if any future type leaves range.
    /// The measured values are stored VERBATIM rather than pre-clamped, so the
    /// table records the measurement and NoiseGenerator's own documented clamp
    /// remains the only place a truncation could happen.
    /// VinylCrackle is NOT in that group once measured properly: its true drive
    /// is +27.283 (applied +7.283, inside range) - see the 60 s note above.
    ///
    /// TOOLCHAIN NOTE: the table was first emitted by g++ 13 (WSL) and re-emitted
    /// by MSVC when the FR-013 forwards landed. The two agree to 0.004 dB on
    /// every entry (largest: VinylRumble +10.9949 vs +10.9909), four orders of
    /// magnitude inside SC-019 (a)'s window, so the entries are NOT re-transcribed
    /// per toolchain - only the two the forwards actually moved are updated.
    ///
    /// ModulationNoise is the one entry that is NOT a measurement: it is
    /// floor-less by construction and renders exactly 0.0f under a zero sidechain
    /// (noise_generator.h:553-558), so its measured "drive" is the +135.227 dB
    /// artefact of audio_features.h's -160 dBFS silence sentinel. FR-012 makes it
    /// unreachable through the public API (it snaps to TapeHiss), so the entry is
    /// pinned at 0.0f and sourceGeneratorDb() can never be called with it.
    static constexpr std::array<float, kNumNoiseTypes> kSourceDriveDb{
        0.0000f,   // White           -24.7727 dBFS   (the reference)
        4.3427f,   // Pink            -29.1155 dBFS
        3.6901f,   // TapeHiss        -28.4629 dBFS  (FR-013 floor forward, see above)
        27.2830f,  // VinylCrackle    -52.0557 dBFS
        0.0000f,   // Asperity        -24.7727 dBFS  (FR-013 floor forward, see above)
        5.9988f,   // Brown           -30.7716 dBFS
        11.9180f,  // Blue            -36.6907 dBFS
        3.0134f,   // Violet          -27.7862 dBFS
        -0.2259f,  // Grey            -24.5469 dBFS
        12.0486f,  // Velvet          -36.8213 dBFS  (at the generator's own
                   //                                 1000 imp/s default, :678)
        10.9949f,  // VinylRumble     -35.7677 dBFS
        0.0000f,   // ModulationNoise NOT MEASURABLE - renders exact silence and
                   //                 is unreachable through FR-012
        6.5209f};  // RadioStatic     -31.2936 dBFS

    /// @brief The constant generator-side level for one type (FR-017).
    [[nodiscard]] static float sourceGeneratorDb(NoiseType type) noexcept {
        const std::size_t idx = static_cast<std::size_t>(type);
        const float drive = (idx < kNumNoiseTypes) ? kSourceDriveDb[idx] : 0.0f;
        return kSourceReferenceDb + drive;
    }

    /// Per-MODEL mix-stage trim, indexed by NoiseOrganismModel (plan S9.1).
    /// kSourceDriveDb structurally cannot reach three of the four models:
    /// GranularDust bypasses NoiseGenerator's level entirely (it uses only the
    /// velvet impulse SIGN), and FilteredWind's bandpass and MetallicHiss's
    /// comb feedback are chain gains that neither FR-017 (per type) nor FR-018
    /// (per Q) compensates. Applied at the MIX stage beside levelRamp, and
    /// deliberately EXCLUDED from getSourceGain, which FR-015 defines as
    /// level x breath x gate - the trim is a calibration property of the source,
    /// not a gain the caller set.
    ///
    /// MEASURED 2026-09-01, not authored, by `NoiseOrganism_MeasureSourceDrive`
    /// (part 2). kModelTrimDb[model] = rmsDbfs(Direct reference render) -
    /// rmsDbfs(model render), everything else identical, GranularDust at the
    /// FR-016 default 100 imp/s. kModelTrimDb[Direct] is 0 BY CONSTRUCTION and
    /// stays 0 - SC-018's "getSourceGain never leaves 1.0" arm depends on the
    /// Direct path being exactly untrimmed, and the reference cannot be trimmed
    /// against itself.
    ///
    /// FIXTURE: ONE ISOLATED SLOT IN THE SC-004 (c) REFERENCE CHAIN - the FR-016
    /// defaults with the reference configuration's 3 resonators, i.e. exactly the
    /// chain SC-001, SC-002, SC-004 and SC-013 are all defined on. Wander is left
    /// ON (FR-016's default) and the figure is the mean square of a 600 s render
    /// averaged over 6 seeds after a 3 s discard, because the quantity being
    /// equalised is the level of the RUNNING organism, not of a frozen snapshot
    /// of it: a `FilteredWind` slot parked at its base cutoff sits ~5 dB below
    /// its own long-run mean (the band-pass is then furthest from the resonator
    /// lines), so a wander-off measurement under-trims that model by that much.
    ///
    /// *** THIS REPLACES A CHAIN-NEUTRALISED MEASUREMENT THAT WAS A DEFECT. ***
    /// The first version of this table was measured with the chain neutralised
    /// (0 resonators, 0 combs, filter opened to 0.45*fs) - the fixture that is
    /// correct for kSourceDriveDb above, which really is a source-side constant
    /// measured on a bare NoiseGenerator. It is the WRONG fixture here, because
    /// for a composed model THE CHAIN IS THE MODEL (FR-020/FR-022 make a
    /// band-pass the definition of `FilteredWind`; FR-040/FR-042 make a
    /// 0.75-feedback comb the definition of `MetallicHiss`), so opening the
    /// filter to 21.6 kHz measures a configuration that never plays and charged
    /// `FilteredWind` a +25.945 dB trim for it. In the FR-016 chain that left the
    /// FilteredWind slot ~20 dB above every other slot; because that slot's
    /// band-pass sweeps +/-3.5 octaves (FR-062's 1.5 plus FR-022's 2.0) across a
    /// signal the resonator stage has already reduced to 2-3 lines a few Hz wide,
    /// the whole organism's level then tracked that one sweep. Measured
    /// consequences of the old table, all of them criterion failures:
    ///   SC-001 (a) worst 10 s window deviation  9.46 dB (limit 3.0)
    ///   SC-002 (c) broadband RMS CV             0.398  (limit 0.06)
    ///   SC-009 (b) 25 ms-frame worst step      16.06 dB (threshold 11.20)
    /// and a MetallicHiss slot at the FR-016 defaults rendering -86.8 dBFS, i.e.
    /// inaudible, which the old note recorded as an unresolved musical problem.
    /// With the table below all four models render within 0.003 dB of each other
    /// in the reference chain, and those three figures become 3.06 dB / 0.148 /
    /// 11.01 dB (threshold 12.16).
    ///
    /// Both plan.md S9.1 and tasks.md T016 item 2 specify this measurement "at
    /// the FR-016 defaults"; the neutralised fixture was a documented departure
    /// from that wording, and this restores it. The one place the fixture goes
    /// beyond the literal FR-016 table is the resonator count (3, not 2): the
    /// trim is a single per-model constant and the count it is measured at is
    /// worth ~3.5 dB on `FilteredWind`, so it is measured in the configuration
    /// every level criterion in this phase is stated on.
    ///
    /// This is the one table the `[.calibration]` case reports as a RESIDUAL
    /// rather than an absolute, because part 2 renders through the organism and
    /// the trim below is already in the path: the printed delta is what to ADD to
    /// the entry, and ~0 is the converged state. Re-measured 2026-09-01 with
    /// these values in place, the deltas are -0.0025 / +0.0005 / -0.0024 dB.
    static constexpr std::array<float, 4> kModelTrimDb{
        0.0000f,    // Direct        (the reference, 0 by construction)
        9.4889f,    // FilteredWind  -60.075 dBFS vs the -50.589 dBFS reference
        -6.8624f,   // GranularDust  -43.727 dBFS
        32.1094f};  // MetallicHiss  -82.696 dBFS
    // The absolute figures above are the UNTRIMMED render of each model in that
    // fixture, i.e. reference - trim. Read the "cannot reach three of the four
    // models" paragraph above against them: every composed model carries a real
    // deficit or excess once its own chain is in the path - GranularDust because
    // it never touches NoiseGenerator's amplitude at all (S6.1 reads only the
    // velvet impulse sign), FilteredWind because its band-pass is parked at
    // 800 Hz over content the resonators put at 70-260 Hz, and MetallicHiss
    // because it pins Blue/Violet (FR-041), which has almost no energy at those
    // same anchors. None of the three is reachable by FR-017's per-type table.

    /// @brief dbToGain(kModelTrimDb[model]), hoisted once per chunk (plan S5.3).
    [[nodiscard]] static float modelTrimGain(NoiseOrganismModel model) noexcept {
        const std::size_t idx = static_cast<std::size_t>(model);
        return dbToGain(idx < kModelTrimDb.size() ? kModelTrimDb[idx] : 0.0f);
    }

    // ---- FR-018 resonator Q make-up (plan S9.2) ----
    // A bandpass with constant 0 dB peak gain admits a broadband source's power
    // in proportion to its noise bandwidth, ENBW ~ f0/Q, so slot RMS falls as
    // 1/sqrt(Q) and the compensation rises with Q.
    //
    // MEASURED 2026-09-01, not authored. Method, reproducible verbatim by
    // `NoiseOrganism_MeasureSourceDrive` (part 3):
    //   bare ResonatorBank, prepare(48000), one resonator enabled, setFrequency
    //   then setQ (that order - after FR-099 setFrequency re-derives Q from the
    //   decay, so the explicit Q must be written last, exactly as
    //   updateResonatorControl does), setGain(0 dB), excited with the FR-016
    //   default source (Brown at kDefaultLevelDb, seed 1), 5 s rendered with the
    //   first 0.5 s discarded for the ring-up. Swept over
    //   Q in {1, 3, 10, 30, 100} at each of the four FR-016 anchors
    //   {70, 140, 260, 500} Hz, then a POOLED least-squares fit of dB vs
    //   log10(Q) with x and y demeaned WITHIN each anchor - the demeaning is what
    //   removes the anchor-dependent offset of a non-white source (Brown/Blue/
    //   Violet are pinned by FR-021/FR-041) and leaves only the slope.
    // Measured slot RMS (dBFS), Q = 1 / 3 / 10 / 30 / 100:
    //    70 Hz : -36.257 -40.146 -45.049 -49.179 -53.247
    //   140 Hz : -35.481 -39.188 -43.775 -48.197 -53.966
    //   260 Hz : -35.963 -39.801 -44.623 -49.001 -53.336
    //   500 Hz : -37.369 -41.429 -46.408 -51.315 -56.742
    // Pooled slope d(RMS)/d(log10 Q) = -9.0806 dB/decade, i.e.
    //   kMakeupSlopeDb = +9.081 - close to, but measurably below, the analytic
    // 10.0 the plan started from (a 0.9 dB error per decade of Q, which at
    // kMaxResonatorQ would have over-compensated by ~0.3 dB).
    static constexpr float kMakeupSlopeDb = 9.081f;
    /// Ceiling on the make-up, so a kMaxResonatorQ resonator - which FR-064's
    /// downward-only Q factor guarantees is the QUIET end - cannot push a slot
    /// above SC-001 (c)'s -3 dBFS. Retained at 12 dB after the measurement and
    /// verified never to engage upward at the FR-016 defaults: with
    /// kMakeupQRef = rt60ToQ(70, 1.5) = 47.75 the largest reachable make-up is
    /// resonatorMakeupDb(kMaxResonatorQ) = 9.081 * log10(100 / 47.75) = +2.92 dB.
    /// The downward clamp still engages (Q = 1 would ask for -35 dB), which is
    /// deliberate: it bounds how far a pathological Q can attenuate a slot.
    static constexpr float kMakeupCeilingDb = 12.0f;
    static constexpr float kMakeupQRef      = rt60ToQ(kDefaultAnchorHz[0], 1.5f);
    static_assert(kMakeupQRef > 0.0f, "make-up reference Q must be positive");

    /// @brief FR-018 make-up gain in dB for one resonator's applied Q.
    [[nodiscard]] static float resonatorMakeupDb(float q) noexcept {
        const float ratio = std::max(q, kMinResonatorQ) / kMakeupQRef;
        const float dB    = kMakeupSlopeDb * std::log10(ratio);
        return std::clamp(sanitise(dB, 0.0f), -kMakeupCeilingDb, kMakeupCeilingDb);
    }

    /// Per-control-step bound on comb-delay motion, as a DISTANCE IN SAMPLES so
    /// the bound is sample-rate independent (SC-008 (c) depends on that).
    ///
    /// This limiter exists because snapSmoothers() removes the bank's own 20 ms
    /// delay smoothing (plan S6.3) - it REPLACES that smoothing rather than
    /// stacking on top of it, which is the amendment to FR-063's stated reasoning
    /// recorded in plan S14.4.
    ///
    /// MEASURED 2026-09-01, not chosen. `NoiseOrganism_MeasureSourceDrive`
    /// (part 4) runs the two measurements this value sits between:
    ///
    /// (i) THE ENVELOPE SWEEP does not bind anywhere reachable. Worst-case bank
    ///     (feedback at kCombFeedbackCap = 0.9, numCombs = 4, the FR-016 base
    ///     delays 16.667/14.344/12.783/11.640 ms), delay driven as a triangle at
    ///     exactly the candidate step per 64-sample control step across the full
    ///     +/-50 % excursion - the fastest trajectory the limiter permits - with
    ///     snapSmoothers() after each push, 60 s rendered, 25 ms-frame envelope
    ///     maxDelta measured after a 1 s discard. Against the SC-009 (b) bound
    ///     (1.5x the same statistic on a delay-static render) EVERY step from
    ///     0.25 to 512 samples passed, on both a White and a Brown excitation:
    ///       White : static 0.7301 dB, threshold 1.0951 dB, worst swept 1.0250 dB
    ///       Brown : static 6.8617 dB, threshold 10.2926 dB, worst swept 6.3712 dB
    ///     Comb-delay motion is a timbral/phase effect that the bank's own
    ///     fractional-delay interpolation already handles; it does not move the
    ///     25 ms envelope at any rate, including rates far beyond the reachable
    ///     ones. So SC-009 (b) cannot produce a "largest passing step" - it
    ///     produces "no step in or beyond the reachable range fails".
    ///
    /// (ii) THE REACHABLE DEMAND is therefore what sets the value. Measured on a
    ///     real PerlinNoiseSource at the fastest legal lane rate (5 cells/s =
    ///     PerlinNoiseSource::kMaxRate) and the widest legal span
    ///     (setCombWander's 50 % clamp ceiling) against the largest legal base
    ///     delay (50 ms, what a 20 Hz fundamental yields), over 64 seeds x 120 s:
    ///       44.1 kHz 21.5467 | 48 kHz 21.5458 | 96 kHz 21.5468 | 192 kHz 21.5479
    ///     samples per control step - flat across sample rate, which is the
    ///     measured confirmation that a bound expressed as a DISTANCE IN SAMPLES
    ///     is sample-rate independent (SC-008 (c) depends on that).
    ///
    /// 24.0 is the smallest swept grid point above that 21.55 sample worst-case
    /// demand, so the limiter is TRANSPARENT to every trajectory reachable
    /// through the public API - which is what SC-002's "realised excursion is at
    /// least 25 % of the configured span" arm needs - while still hard-bounding
    /// delay motion at 0.5 ms per control step at 48 kHz against a configuration
    /// jump or a substituted value. The plan's 0.25-sample fallback would have
    /// clipped the reachable trajectory by ~86x, and its "~8 samples/step"
    /// estimate of the unlimited demand was low by 2.7x; both are superseded by
    /// the figures above.
    static constexpr float kMaxCombDelayStepSamples = 24.0f;

    /// Mix trim: kMaxSources incoherent slots sum at +10*log10(kMaxSources) dB,
    /// so the mix is scaled by 1/sqrt(kMaxSources) to keep a full organism at the
    /// same nominal level as a single slot (FR-074).
    static constexpr float kMixScale = 0.5f;
    static_assert(kMaxSources == 4, "kMixScale is 1/sqrt(kMaxSources) - re-derive "
                                    "it if kMaxSources ever changes");

    /// Saturation ceiling for the FR-074 clamp counter. Written out rather than
    /// taken from <limits> so the header keeps its current include set.
    static constexpr std::uint32_t kMaxClampCount = 0xFFFFFFFFu;

    /// One-pole coefficient for the FR-015 source-RMS diagnostic, per control
    /// step. 0.25 gives a ~4-step (5.3 ms at 48 kHz) time constant - fast enough
    /// for SC-010 (a)'s 250 ms post-wake window, slow enough not to track the
    /// noise itself.
    static constexpr float kSourceRmsSmoothCoeff = 0.25f;

    // =========================================================================
    // Lifecycle internals (plan S2)
    // =========================================================================

    /// @brief The steady gate target for a slot (plan S8.3). A slot dropped by
    /// setNumSources is silenced exactly like a dormant one.
    [[nodiscard]] float gateSteady(std::size_t slotIndex) const noexcept {
        if (!validSlot(slotIndex)) {
            return 0.0f;
        }
        const Slot& s = slots_[slotIndex];
        return (s.dormant || slotIndex >= config_.numSources) ? 0.0f : s.wakeAmount;
    }

    /// @brief The FR-042 inharmonic comb ratio law, evaluated by the organism.
    /// Not TimeVaryingCombBank::Tuning::Inharmonic: writing setCombDelay puts the
    /// bank in Tuning::Custom unconditionally (timevar_comb_bank.h:515), and
    /// FR-063 writes setCombDelay on every control step.
    void updateCombBaseDelays(Slot& s) const noexcept {
        for (std::size_t n = 0; n < kMaxCombsPerSource; ++n) {
            const float ratio =
                std::sqrt(1.0f + static_cast<float>(n) * s.combSpread);
            const float hz =
                std::max(kMinResonatorFrequency, s.combFundamental * ratio);
            s.combBaseDelayMs[n] =
                std::clamp(1000.0f / hz, 1.0f, config_.maxCombDelayMs);
        }
    }

    // =========================================================================
    // The FR-013 duck FSM (plan S8.3)
    // =========================================================================

    /// @brief Route a model/type write through change detection and the duck.
    ///
    /// Why a duck exists at all: each NoiseType's contribution to the generator's
    /// output is gated on `if (noiseEnabled_[idx])` (noise_generator.h:388 ...
    /// :568), so disabling a type removes a full-amplitude broadband contribution
    /// on the VERY NEXT SAMPLE - updateLevelTarget's ramp to zero (:578-584) never
    /// gets to run. The organism therefore silences its own gate first.
    void requestSourceState(std::size_t slotIndex, NoiseOrganismModel model,
                            NoiseType requestedType) noexcept {
        Slot& s = slots_[slotIndex];

        // Where the slot is HEADED: the pending goal while a duck is in flight,
        // its applied state otherwise. Comparing against the goal (not the applied
        // value) is what makes a repeated write during a duck a no-op too.
        const NoiseOrganismModel goalModel = s.duckPending ? s.pendingModel : s.model;
        const NoiseType goalRequested      = s.duckPending ? s.pendingType : s.requestedType;

        // FR-012's ModulationNoise substitution - and each composed model's pinned
        // base type - is applied FIRST, so the change detection below compares
        // EFFECTIVE values, the only ones the generator can hear.
        const NoiseType goalEffective =
            effectiveNoiseType(goalModel, goalRequested, s.hissViolet);
        const NoiseType newEffective = effectiveNoiseType(model, requestedType, s.hissViolet);

        if (model == goalModel && newEffective == goalEffective) {
            // A full no-op for the generator: the duck is NOT armed and
            // NoiseGenerator is NOT touched. A parameter-echoing host that rewrites
            // its whole surface every block must not duck the slot once per block
            // (SC-018's coalescing arm).
            //
            // The REQUESTED type is still remembered, because FR-012 restores it
            // when the slot returns to Direct - writing Pink to a MetallicHiss slot
            // changes nothing audible (the model pins Blue) but must survive.
            if (s.duckPending) {
                s.pendingType = requestedType;
            } else {
                s.requestedType = requestedType;
            }
            return;
        }

        s.pendingModel = model;
        s.pendingType  = requestedType;
        s.duckPending  = true;

        if (!prepared_) {
            // No render loop exists to reach the swap point, and the organism is
            // silent anyway - so the change lands immediately and the FR-015
            // getters stay honest before prepare().
            applyPendingSourceState(slotIndex);
            s.duckState = Duck::Idle;
            return;
        }

        if (s.duckState != Duck::Down) {
            // Idle OR Up. RE-ARMING FROM Up IS MANDATORY: without it the Down leg
            // is over, the swap condition never fires again, and this write is
            // silently LOST while the getters keep reporting the old value. It
            // costs a second duck for a change that arrives after the swap point,
            // which is correct - that is a genuinely new transition, not part of
            // the burst the coalescing guarantee covers.
            s.duckState = Duck::Down;
            s.gate.configure(kGainRampMs * 0.5f, static_cast<float>(sampleRate_));
            s.gate.setTarget(0.0f);
        }
        // Down: COALESCE. The pending target above is updated and the ramp is NOT
        // restarted, so a burst of writes arriving before the swap costs exactly
        // one kGainRampMs duck rather than one per write.
    }

    /// @brief The swap itself, run on the exact sample where the gate reads zero.
    ///
    /// On the zero sample rather than at the next control-step boundary: both
    /// satisfy FR-013, but the exact-zero form keeps the total duration exactly
    /// rampSamples_, which is what SC-018's +/-5 ms and SC-009 (a)'s monotonicity
    /// want. It is allocation-free - setNoiseEnabled writes a bool and a smoother
    /// target (noise_generator.h:255-261).
    void applyPendingSourceState(std::size_t slotIndex) noexcept {
        Slot& s = slots_[slotIndex];
        // The OLD type off first, so the generator is never running two at once.
        s.generator.setNoiseEnabled(s.activeType, false);
        s.model         = s.pendingModel;
        s.requestedType = s.pendingType;
        s.activeType    = effectiveNoiseType(s.model, s.requestedType, s.hissViolet);
        s.duckPending   = false;
        // The rest of the swap - enabling the new type at
        // kSourceReferenceDb + kSourceDriveDb[activeType] (the SAME constant
        // expression applyConfiguration pushes, with NO levelDb term) and applying
        // the new model's chain configuration - goes through the single owner of
        // those writes rather than being duplicated here. A second, disagreeing
        // copy of that level expression is exactly the defect plan S8.3 records:
        // it would render a -12 dB slot at -24 dB after its first model change,
        // uniformly across all 13 types, where no criterion could see it.
        applySlotConfiguration(slotIndex);
    }

    /// @brief Advance the duck FSM by one sample, given the gate value that was
    /// just produced for this sample.
    void serviceDuck(std::size_t slotIndex, float gateValue) noexcept {
        Slot& s = slots_[slotIndex];
        if (s.duckState == Duck::Down) {
            // Exact comparison is correct: LinearRamp lands exactly on its target
            // (smoother.h:380-383) and the target here is a literal 0.0f.
            if (gateValue == 0.0f) {
                applyPendingSourceState(slotIndex);
                s.duckState = Duck::Up;
                s.gate.configure(kGainRampMs * 0.5f, static_cast<float>(sampleRate_));
                s.gate.setTarget(gateSteady(slotIndex));
            }
        } else if (s.duckState == Duck::Up) {
            if (s.gate.isComplete()) {
                s.duckState = Duck::Idle;
            }
        }
    }

    /// @brief Advance BOTH per-sample gain ramps one sample, service the duck, and
    /// return the FR-050 applied gain for this sample.
    ///
    /// Called from both renderChunk paths - the audible one and the chain-skip one
    /// - so the ramps and the FSM stay a pure function of the sample count however
    /// the caller partitions its blocks (SC-016), and so a duck armed on a dormant
    /// slot (whose gate is already at zero) still reaches its swap.
    ///
    /// The ramps advance PER SAMPLE and are never held across the control chunk: a
    /// 1.33 ms staircase on the one signal whose monotonicity is a criterion is
    /// not acceptable. breathGain is the exception and is held by design - its own
    /// motion is under 0.1 dB per chunk at any legal setting.
    [[nodiscard]] float advanceSlotGain(std::size_t slotIndex) noexcept {
        Slot&       s         = slots_[slotIndex];
        const float levelGain = s.levelRamp.process();
        const float gateGain  = s.gate.process();
        serviceDuck(slotIndex, gateGain);
        return levelGain * s.breathGain * gateGain;
    }

    /// @brief The FR-070 affine breathing map, recomputed once per control step.
    ///
    /// BreathingModulator is BIPOLAR [-1, +1] (breathing_modulator.h:103,
    /// getSourceRange() at :227-229), so a bare multiply would invert the slot on
    /// every exhale. The affine form is exactly 1.0 at b == 0 - so depth 0 is
    /// neutral rather than a constant duck - and lies inside [0.55, 1.45] for every
    /// legal depth (+/-0.92 dB at the FR-016 default depth of 0.25).
    ///
    /// BreathingModulator::setDepth stays at its library 1.0f (:112): the organism
    /// owns depth here, and forwarding it as well would square it.
    static void updateBreathGain(Slot& s) noexcept {
        const float b   = laneValue(s.breathing);
        s.breathGain    = 1.0f + kBreathGainSpan * s.breathDepth * b;
    }

    /// @brief Restore every configuration member to the FR-016 normative table.
    /// Called by prepare() ONLY - reset() is configuration-preserving (FR-004),
    /// so prepare() is the single path back to the defaults (FR-002).
    /// PrepareConfig fields are excluded: prepare() has already clamped them from
    /// the caller's request.
    void applyDefaults() noexcept {
        wanderEnabled_ = true;

        for (Slot& s : slots_) {
            s.model         = NoiseOrganismModel::Direct;
            s.requestedType = NoiseType::Brown;
            s.activeType    = NoiseType::Brown;
            s.levelDb       = -12.0f;
            s.numResonators = 2;
            s.numCombs      = 2;
            s.anchorHz      = kDefaultAnchorHz;
            s.decaySeconds  = 1.5f;

            s.combFundamental = 60.0f;
            s.combSpread      = 0.35f;
            // The latch goes back to false with the value: prepare() is the one
            // documented return to the FR-016 defaults (FR-002), so the per-model
            // rule must govern again afterwards.
            s.combFeedback        = kDefaultCombFeedback;
            s.combFeedbackUserSet = false;
            s.combBaseDelayMs.fill(0.0f);
            s.appliedCombDelayMs.fill(0.0f);

            s.filterBaseCutoffHz = 800.0f;
            s.filterBaseQ        = 0.7f;

            s.resWanderSemis  = 2.0f;
            s.resQWander      = 0.25f;
            s.cutoffWanderOct = 1.5f;
            s.resonanceWander = 0.2f;
            s.combWanderPct   = 12.0f;

            s.breathDepth        = 0.25f;
            s.breathIrregularity = 0.3f;

            s.dustGrainMsRequested = 40.0f;
            s.dustDensityRequested = 100.0f;
            s.dustColor            = NoiseColor::Brown;
            // Derives dustDensityEffective, dustGrainMsEffective AND the FR-036
            // dustGrainGain from the two requests above - never assigned here,
            // so the defaults cannot disagree with the rule (at 100 imp/s x
            // 40 ms the mean concurrency is 4 and the gain is 0.5, not 1).
            updateDustEffective(s);

            s.hissViolet = false;
            s.dormant    = false;
            s.wakeAmount = 1.0f;

            s.appliedResHz.fill(0.0f);
            s.appliedResQ.fill(0.0f);
            s.appliedCutoffHz   = s.filterBaseCutoffHz;
            s.breathGain        = 1.0f;
            s.sourceRmsSmoothed = 0.0f;
            s.sourceSumSq       = 0.0f;
            s.sourceSumCount    = 0;

            s.duckState    = Duck::Idle;
            s.duckPending  = false;
            s.pendingModel = NoiseOrganismModel::Direct;
            s.pendingType  = NoiseType::Brown;

            s.grains.fill(DustGrain{});
            s.grainCursor = 0;

            s.breathing.setIrregularity(s.breathIrregularity);
        }

        // FR-069: every lane rate/smoothness is DERIVED from the one scalar
        // through its SINGLE owner, setWanderRate() - never recomputed here, so
        // prepare() and a later caller cannot disagree about the seconds ->
        // normalised mapping (plan S7.2). Safe at this point in prepare(): every
        // sub-component in every slot has just been prepared above, and
        // setWanderRate touches only lane/filter parameters, never buffers.
        setWanderRate(kDefaultWanderRateHz);
    }

    /// @brief Push one slot's configuration into its sub-components (plan S2.3).
    /// Silent no-op before prepare(): a setter must never reach an un-prepared
    /// sub-component.
    void applySlotConfiguration(std::size_t slotIndex) noexcept {
        if (!prepared_ || !validSlot(slotIndex)) {
            return;
        }
        Slot& s = slots_[slotIndex];

        // ---- source ---------------------------------------------------------
        // Exactly one NoiseType enabled, at a CONSTANT per-type level. levelDb
        // never appears here (plan S9.1's single-owner rule).
        const auto activeIdx = static_cast<std::size_t>(s.activeType);
        for (std::size_t t = 0; t < kNumNoiseTypes; ++t) {
            s.generator.setNoiseEnabled(static_cast<NoiseType>(t), t == activeIdx);
        }
        s.generator.setNoiseLevel(s.activeType, sourceGeneratorDb(s.activeType));
        s.generator.setMasterLevel(0.0f);
        // FR-013's model-specific forwards for the two FLOORED signal-dependent
        // types. Unconditional: they are constant configuration, cost two
        // clamped stores, and pushing them only on the matching activeType would
        // make the applied floor depend on the order types were selected in.
        // The rationale for the value, and why the library defaults make
        // SC-019 (a) unreachable, is at kSignalDependentFloorDb.
        s.generator.setTapeHissParams(kSignalDependentFloorDb, kSignalDependentSensitivity);
        s.generator.setAsperityParams(kSignalDependentFloorDb, kSignalDependentSensitivity);
        // setCrackleParams (noise_generator.h:333) is the third FR-013 forward
        // and is deliberately NOT written: VinylCrackle's library defaults
        // (3 clicks/s, -42 dB surface, :109) are the configuration FR-017's
        // drive entry is measured at and they need no correction, so forwarding
        // them would only duplicate the defaults at a second site.
        // The GranularDust TRIGGER rate (FR-031, FR-035), and ONLY on a dust
        // slot. setDustDensity is a parameter of that model, not of the Velvet
        // NoiseType: a Direct slot that selects NoiseType::Velvet keeps
        // NoiseGenerator's own 1000 imp/s default (noise_generator.h:678), which
        // is the density FR-017's kSourceDriveDb table is measured at (T016
        // measures a BARE generator). Pushing the dust default of 100 there
        // instead would put that one type 10 dB below its own calibration with
        // nothing in the drive table to account for it.
        // The forward is exact and never re-clamps: setVelvetDensity's own range
        // is [100, 20000] (:340), the range dustDensityEffective already sits in.
        if (s.model == NoiseOrganismModel::GranularDust) {
            s.generator.setVelvetDensity(s.dustDensityEffective);
        }
        s.carrier.setColor(s.dustColor);

        // ---- resonators -----------------------------------------------------
        for (std::size_t i = 0; i < kMaxResonators; ++i) {
            s.resonators.setEnabled(i, i < s.numResonators);
        }
        for (std::size_t i = 0; i < s.numResonators; ++i) {
            // Frequency FIRST, then decay: both re-derive Q from the other
            // (resonator_bank.h:333 after FR-099, :352), so this order leaves Q
            // consistent with BOTH. setQ is the control step's sole writer of Q
            // (plan S6.2) and setDecay is never called again after this point.
            s.resonators.setFrequency(i, s.anchorHz[i]);
            s.resonators.setDecay(i, s.decaySeconds);
        }

        // ---- combs ----------------------------------------------------------
        // The delays come from the organism's OWN evaluation of the bank's
        // documented inharmonic law (updateCombBaseDelays -> FR-042), never from
        // Tuning::Inharmonic: setCombDelay forces Tuning::Custom unconditionally
        // (timevar_comb_bank.h:515) and the control step writes it every 64
        // samples, so the bank is in Custom from the first step onwards and
        // setFundamental/setSpread would stop governing anything. This is also
        // where MetallicHiss's hotter feedback default reaches the audio, via
        // effectiveCombFeedback().
        //
        // setModDepth / setRandomModulation are NEVER called - they stay at the
        // library's 0.0f (:414, :416). The bank has no setSeed and hard-seeds
        // every per-comb PRNG at 12345u + i*7919u (:466, :487), so its internal
        // motion would be bit-identical across all four slots; all comb motion
        // therefore comes from the salted FR-063 Perlin lanes. Leaving them at 0
        // is also the first condition of processBlock's hoisted path (:728).
        updateCombBaseDelays(s);
        if (s.numCombs > 0) {
            s.combs.setNumCombs(
                std::clamp(s.numCombs, std::size_t{1}, kMaxCombsPerSource));
            for (std::size_t i = 0; i < s.numCombs; ++i) {
                s.combs.setCombDelay(i, s.combBaseDelayMs[i]);
                s.combs.setCombFeedback(i, effectiveCombFeedback(s));
                s.combs.setCombGain(i, 0.0f);
                s.combs.setCombDamping(i, 0.0f);
                s.appliedCombDelayMs[i] = s.combBaseDelayMs[i];
            }
        }
        // Load-bearing, not an optimisation: processBlock takes the hoisted path
        // only when every smoother reports isComplete() (timevar_comb_bank.h:728-741),
        // and the delay smoother's 20 ms constant (:109) would otherwise keep the
        // bank permanently on the ~3x more expensive per-sample path.
        s.combs.snapSmoothers();

        // ---- chain filter ---------------------------------------------------
        // FR-022 IS this block: FilteredWind is Brown noise (pinned upstream by
        // effectiveNoiseType) through a BAND-PASS StochasticFilter whose cutoff
        // wanders over 2 octaves with 400 ms smoothing - no new DSP, only a
        // different configuration of the stage every model already runs.
        //
        // Every value here is written EXPLICITLY rather than inherited: a stock
        // StochasticFilter wanders +/-2 octaves at 1 Hz (kDefaultChangeRate,
        // stochastic_filter.h:103; kDefaultOctaveRange, :112) with
        // cutoffRandomEnabled_ already true (:555), an order of magnitude faster
        // than anything this component is about (FR-016, FR-056).
        // setCutoffRandomEnabled follows the FR-068 master switch rather than a
        // literal true, because zeroing the external spans alone would leave that
        // internal randomiser running with no control arm reachable (plan S7.3).
        const bool wind = (s.model == NoiseOrganismModel::FilteredWind);
        s.filter.setMode(RandomMode::Walk);
        s.filter.setBaseFilterType(wind ? SVFMode::Bandpass : SVFMode::Lowpass);
        s.filter.setBaseCutoff(s.filterBaseCutoffHz);
        s.filter.setBaseResonance(s.filterBaseQ);
        s.filter.setCutoffRandomEnabled(wanderEnabled_);
        s.filter.setResonanceRandomEnabled(false);
        s.filter.setTypeRandomEnabled(false);
        s.filter.setCutoffOctaveRange(wind ? 2.0f : 1.0f);
        s.filter.setSmoothingTime(wind ? 400.0f : 200.0f);
        s.filter.setChangeRate(wanderRateHz_);

        // ---- lanes ----------------------------------------------------------
        // Every lane runs at unit depth permanently; the organism's own span
        // constants scale the [-1,+1] output, so a span of 0 freezes a parameter
        // while the lane keeps advancing (FR-066).
        for (BrownianDrift& lane : s.resFreqLane) {
            lane.setDepth(1.0f);
            lane.setMean(0.0f);
        }
        s.cutoffLane.setDepth(1.0f);
        s.cutoffLane.setMean(0.0f);
        s.resonanceLane.setDepth(1.0f);
        s.resonanceLane.setMean(0.0f);
        // setMean is a BrownianDrift member alone (brownian_drift.h:165):
        // PerlinNoiseSource and BreathingModulator declare no mean/bias setter,
        // so the same call on them would not compile. Both are zero-mean by
        // construction (Perlin lattice, sinusoidal breath).
        for (PerlinNoiseSource& lane : s.combLane) {
            lane.setDepth(1.0f);
        }
        // BreathingModulator::setDepth stays at its library 1.0 - the organism
        // owns depth in the FR-070 affine map and forwarding it would square it.
        s.breathing.setDepth(1.0f);
    }

    /// @brief Push every slot's configuration. The one routine prepare(),
    /// reset() and every configuration-changing setter go through.
    void applyConfiguration() noexcept {
        for (std::size_t i = 0; i < kMaxSources; ++i) {
            applySlotConfiguration(i);
        }
    }

    /// Warm-up length for settleSourceLevelSmoothers(). NoiseGenerator smooths
    /// its levels over 5 ms (noise_generator.h:139) and OnePoleSmoother snaps
    /// exactly onto its target once inside kCompletionThreshold = 1e-4
    /// (smoother.h:55, :196-201), which a -20 dB target reaches in ~360 samples
    /// at 48 kHz. 50 ms is ~6x that at every supported rate.
    static constexpr float kSourceSettleMs = 50.0f;

    /// @brief Put each slot's chain filter back on the CONFIGURED base.
    ///
    /// Called immediately after applyConfiguration() by BOTH prepare() and
    /// reset(), and that placement is the whole point: StochasticFilter::reset()
    /// snaps its cutoff/resonance smoothers to whatever base is current at the
    /// moment it runs (stochastic_filter.h:209-211), while setBaseCutoff only
    /// STORES the base and never touches the smoother (:343-346). The control
    /// step writes the DRIFTED cutoff into that base every 64 samples
    /// (updateFilterControl), so a reset() taken before the configuration is
    /// re-pushed leaves the smoother sitting on the last drifted value - and a
    /// freshly prepared instance leaves it on the library default, because
    /// prepare()'s own snap happens before applyConfiguration() has pushed
    /// anything. Resetting AFTER the push lands both on the same value, which is
    /// what makes reset() reproduce the post-prepare stream (FR-004, SC-006 (a)).
    void snapChainFiltersToConfiguration() noexcept {
        for (Slot& slot : slots_) {
            slot.filter.reset();
        }
    }

    /// @brief Settle every slot's NoiseGenerator level smoothers, then clear the
    /// state the settling dirtied. prepare() ONLY.
    ///
    /// NoiseGenerator::reset() clears filters, envelopes and the RNG but touches
    /// no level smoother (noise_generator.h:186-231), and prepare() leaves all of
    /// them at 0 with a 5 ms time constant (:139-148). A freshly prepared organism
    /// would therefore fade its source in over the first ~8 ms while an organism
    /// that has been rendering does not, so reset() could never reproduce the
    /// post-prepare stream. Rendering the warm-up here settles them; the snap at
    /// smoother.h:196-201 makes "settled" bit-exact rather than merely close, so
    /// the two states are EQUAL and not just similar. The generator.reset()
    /// afterwards clears the colour-filter state and rewinds the RNG advance the
    /// warm-up caused, leaving precisely the state a reset() after real rendering
    /// leaves - and prepare()'s final setSeed(seed_) re-derives the stream seeds
    /// after this runs, so the rendered audio is unchanged.
    ///
    /// NOT called from reset(): this is a 50 ms render per slot, far too
    /// expensive for an RT-callable method, and reset() does not need it - it
    /// follows rendering that has already settled the very same smoothers.
    void settleSourceLevelSmoothers() noexcept {
        const auto settleSamples = static_cast<std::size_t>(std::lround(
            static_cast<double>(kSourceSettleMs) * 0.001 * sampleRate_));
        for (Slot& slot : slots_) {
            std::size_t remaining = settleSamples;
            while (remaining > 0) {
                const std::size_t chunk = std::min(remaining, scratchA_.size());
                slot.generator.process(scratchA_.data(), chunk);
                remaining -= chunk;
            }
            slot.generator.reset();
        }
        scratchA_.fill(0.0f);
    }

    /// @brief The S12 memory formula, evaluated from the lengths prepare()
    /// requested. Self-reported because AllocationDetector has no byte
    /// accounting (tests/test_helpers/allocation_detector.h:83-89).
    ///
    /// Default (48 kHz, 50 ms): trunc(2400) + 1 -> nextPowerOf2 = 4096 floats
    /// = 16 KiB per comb line x 8 lines = 128 KiB per slot x 4 slots
    /// = 512 KiB, + the 8 KiB dust table = 532 480 B (520 KiB).
    /// Worst case (192 kHz, 200 ms): 38 400 + 1 -> 65 536 floats = 256 KiB per
    /// line => 8 MiB per organism - documented so a Phase-10 voice count is
    /// chosen against the real number.
    [[nodiscard]] std::size_t computeAllocatedBytes() const noexcept {
        const auto maxDelaySamples = static_cast<std::size_t>(
            sampleRate_ * static_cast<double>(config_.maxCombDelayMs) / 1000.0);
        const std::size_t lineFloats = nextPowerOf2(maxDelaySamples + 1);
        const std::size_t combBytesPerSlot =
            TimeVaryingCombBank::kMaxCombs * lineFloats * sizeof(float);
        return kMaxSources * combBytesPerSlot +
               kDustEnvelopeTableSize * sizeof(float);
    }

    // =========================================================================
    // Render internals (plan S5)
    // =========================================================================

    /// @brief Re-target every slot's gate at its steady value over kGainRampMs
    /// (plan S8.3). The single path for every dormancy / wake / numSources
    /// change, so all three fade with the identical FR-073 ramp.
    ///
    /// A slot whose duck is on its DOWN leg is skipped, and that is load-bearing
    /// rather than tidy: re-targeting there would abort the descent, the swap
    /// point (gate exactly 0) would never be reached and the pending type/model
    /// write would be LOST. The swap re-targets gateSteady() itself, so the new
    /// dormancy / wake / count is picked up one leg later with nothing dropped.
    void refreshGates() noexcept {
        const auto sampleRateF = static_cast<float>(sampleRate_);
        for (std::size_t i = 0; i < kMaxSources; ++i) {
            Slot& s = slots_[i];
            if (s.duckState == Duck::Down) {
                continue;  // the duck owns the gate until it reaches zero
            }
            s.gate.configure(kGainRampMs, sampleRateF);
            s.gate.setTarget(gateSteady(i));
        }
    }

    /// @brief Does this slot still need its resonator/comb/filter stages run?
    ///
    /// Gated on the GATE RAMP reaching exactly zero, not on the dormant flag
    /// (plan S5.4): while the gate is still travelling, the chain must keep
    /// running so the fade is a fade of real audio. The source stage (and the
    /// lanes) run regardless - FR-071's "source runs, chain skipped".
    [[nodiscard]] static bool chainActive(const Slot& s) noexcept {
        // Exact comparison is correct here: gateSteady() assigns a literal 0.0f
        // and LinearRamp lands exactly on its target (smoother.h:380-383).
        return !(s.gate.isComplete() && s.gate.getCurrentValue() == 0.0f);
    }

    /// @brief 1 when the FR-068 master switch is on, 0 when it is off.
    /// Every EXTERNAL lane span is multiplied by this; the lanes themselves keep
    /// advancing either way (FR-066).
    [[nodiscard]] float wanderScale() const noexcept {
        return wanderEnabled_ ? 1.0f : 0.0f;
    }

    /// @brief Advance every one of the slot's wander lanes by EXACTLY
    /// kControlChunkSamples (plan S7, step 1 of S5.2).
    ///
    /// Unconditional, by contract:
    ///   * a lane whose organism-level span is 0 still advances, so raising the
    ///     span back up resumes the trajectory the always-on lane would have
    ///     been on rather than jumping from the frozen base (FR-066);
    ///   * a DORMANT slot's lanes still advance, so waking never runs a control
    ///     period on the coefficients the slot went dormant with (FR-071);
    ///   * the FR-068 master switch does not stop them either - it scales the
    ///     spans (wanderScale()), it does not freeze the motion (plan S7.3).
    ///
    /// The FIXED 64-sample step is mandatory, not stylistic:
    /// BreathingModulator::processBlock(n) advances its phase ONCE per call by
    /// `n` samples and re-targets its output smoother once
    /// (breathing_modulator.h:208-215), so it is NOT partition-invariant; calling
    /// it only in 64-sample units on the absolute control grid makes it so, which
    /// is what SC-016 rests on. BrownianDrift (brownian_drift.h:194-209) and
    /// PerlinNoiseSource (perlin_noise_source.h:291-311) are bit-identical under
    /// any partitioning, but use the same 64 for uniformity.
    static void advanceLanes(Slot& s) noexcept {
        for (BrownianDrift& lane : s.resFreqLane) {
            lane.processBlock(kControlChunkSamples);
        }
        s.cutoffLane.processBlock(kControlChunkSamples);
        s.resonanceLane.processBlock(kControlChunkSamples);
        for (PerlinNoiseSource& lane : s.combLane) {
            lane.processBlock(kControlChunkSamples);
        }
        // Advanced here with the rest; the FR-070 affine map that turns its
        // output into breathGain is tasks.md T013's, so breathGain stays at 1.0
        // until then. The lane must run from T012 regardless, because a lane that
        // only starts advancing when its consumer lands would make the FR-070
        // gain jump at that moment.
        s.breathing.processBlock(kControlChunkSamples);
    }

    /// @brief Read one lane once, guarded (FR-008), clamped to its [-1, +1]
    /// documented range. Templated on the concrete lane type deliberately: the
    /// `ModulationSource` base declares getCurrentValue() virtual, and calling it
    /// through a base reference would put a virtual dispatch on the control path
    /// for no benefit (FR-006 / plan S5.5).
    template <typename LaneT>
    [[nodiscard]] static float laneValue(const LaneT& lane) noexcept {
        return std::clamp(sanitise(lane.getCurrentValue(), 0.0f), -1.0f, 1.0f);
    }

    /// @brief Fold the control period's accumulated source energy into the
    /// FR-015 RMS diagnostic and clear the accumulator (plan S5.6).
    static void foldSourceRms(Slot& s) noexcept {
        if (s.sourceSumCount == 0) {
            return;  // first control step of a render: nothing accumulated yet
        }
        const float meanSquare =
            s.sourceSumSq / static_cast<float>(s.sourceSumCount);
        const float rms = std::sqrt(std::max(0.0f, sanitise(meanSquare, 0.0f)));
        s.sourceRmsSmoothed = detail::flushDenormal(
            s.sourceRmsSmoothed + kSourceRmsSmoothCoeff * (rms - s.sourceRmsSmoothed));
        s.sourceSumSq    = 0.0f;
        s.sourceSumCount = 0;
    }

    /// @brief Resonator control step (plan S6.2). setQ is the SOLE per-step
    /// writer of qValues_: setDecay assigns the same variable
    /// (resonator_bank.h:349 vs :383) and is called once at configuration time
    /// and never again, so the two can never destroy each other.
    ///
    /// setSpectralTilt stays at exactly 0.0f and is not exposed - at non-zero
    /// tilt calculateTiltGain costs a std::log2 plus a dbToGain PER RESONATOR PER
    /// SAMPLE (resonator_bank.h:120-126, called at :504); at zero it early-returns
    /// 1.0f. setDamping and setExciterMix likewise stay at their defaults.
    void updateResonatorControl(Slot& s) noexcept {
        const float semis  = wanderScale() * s.resWanderSemis;
        const float qDepth = wanderScale() * s.resQWander;
        for (std::size_t i = 0; i < s.numResonators; ++i) {
            const float lane   = laneValue(s.resFreqLane[i]);
            const float anchor = s.anchorHz[i];
            const float driftedHz =
                std::clamp(sanitise(anchor * std::exp2(semis * lane / 12.0f), anchor),
                           kMinResonatorFrequency, maxResonatorHz());
            // FR-064 is DOWNWARD ONLY: rt60ToQ saturates at kMaxResonatorQ for
            // every f * RT60 > 219.9 (resonator_bank.h:92-98), which at the
            // FR-016 default decay already covers the top two anchors, so an
            // upward factor would simply be clipped away.
            const float nominalQ = rt60ToQ(driftedHz, s.decaySeconds);
            const float qFactor  = 1.0f - kQWanderSpan * qDepth * (1.0f + lane) * 0.5f;
            const float targetQ  = std::clamp(sanitise(nominalQ * qFactor, nominalQ),
                                              kMinResonatorQ, kMaxResonatorQ);
            s.resonators.setFrequency(i, driftedHz);
            s.resonators.setQ(i, targetQ);
            s.resonators.setGain(i, resonatorMakeupDb(targetQ));
            s.appliedResHz[i] = driftedHz;
            s.appliedResQ[i]  = targetQ;
        }
    }

    /// @brief Comb control step (plan S6.3), ending in the load-bearing
    /// snapSmoothers().
    ///
    /// snapSmoothers() is NOT an optimisation: TimeVaryingCombBank::processBlock
    /// takes its hoisted path only when modDepth_ == 0 AND every smoother reports
    /// isComplete() (timevar_comb_bank.h:728-741). Writing setCombDelay on every
    /// control step keeps the 20 ms delay smoother (kDelaySmoothingMs, :109)
    /// permanently unsettled, pinning the bank to the per-sample path
    /// (~99,000 ns per 512-block for 8 combs against a 106,666 ns budget, versus
    /// ~30,700 ns hoisted). The bank's own doc endorses this usage (:352-357) and
    /// ContinuousBody is the shipped precedent.
    void updateCombControl(Slot& s) noexcept {
        if (s.numCombs == 0) {
            return;  // stage skipped entirely; the bank is never touched
        }
        // Snapping moves the continuity obligation onto the organism, so the
        // slew bound below REPLACES the smoothing the snap removed.
        const float maxStepMs =
            kMaxCombDelayStepSamples * 1000.0f / static_cast<float>(sampleRate_);
        const float pct = wanderScale() * s.combWanderPct;
        for (std::size_t n = 0; n < s.numCombs; ++n) {
            const float base = s.combBaseDelayMs[n];
            const float lane = laneValue(s.combLane[n]);
            float       targetMs =
                std::clamp(sanitise(base * (1.0f + 0.01f * pct * lane), base),
                           1.0f, config_.maxCombDelayMs);
            const float previousMs = s.appliedCombDelayMs[n];
            targetMs = std::clamp(targetMs, previousMs - maxStepMs,
                                  previousMs + maxStepMs);
            s.combs.setCombDelay(n, targetMs);
            // Both the slew limiter's previous state AND the FR-015
            // getCombCurrentDelayMs echo, so the realised trajectory is
            // observable rather than inferred.
            s.appliedCombDelayMs[n] = targetMs;
        }
        s.combs.snapSmoothers();
    }

    /// @brief Chain-filter control step (plan S6.4). The filter's own internal
    /// randomiser is a different KIND of motion (discrete Walk jumps around the
    /// base, FR-023) and is left alone here; FR-068's master switch is the only
    /// thing that turns it off.
    void updateFilterControl(Slot& s) noexcept {
        const float cutoffLaneValue = laneValue(s.cutoffLane);
        const float octaves         = wanderScale() * s.cutoffWanderOct;
        const float hz              = std::clamp(
            sanitise(s.filterBaseCutoffHz * std::exp2(octaves * cutoffLaneValue),
                                  s.filterBaseCutoffHz),
            20.0f, maxFilterHz());

        const float resonanceLaneValue = laneValue(s.resonanceLane);
        const float resonanceSpan      = wanderScale() * s.resonanceWander;
        const float q                  = std::clamp(
            sanitise(s.filterBaseQ * (1.0f + resonanceSpan * resonanceLaneValue),
                                     s.filterBaseQ),
            SVF::kMinQ, SVF::kMaxQ);

        s.filter.setBaseCutoff(hz);
        s.filter.setBaseResonance(q);
        s.appliedCutoffHz = hz;
    }

    /// @brief One control step, on the absolute 64-sample grid (plan S5.2).
    ///
    /// Runs in full for EVERY slot, including dormant and dropped ones. That is
    /// what makes the FR-015 applied-state echo freewheel as SC-010 (a) requires,
    /// and it also keeps each sub-component's state in lockstep with the echo, so
    /// a slot that wakes never runs one control period on the coefficients it
    /// went dormant with. FR-071's dormancy saving comes from renderChunk's
    /// chain skip (the per-sample work), not from withholding control writes.
    ///
    /// Order matters: advanceLanes() FIRST, so every read below is this step's
    /// lane value and not the previous step's.
    void updateControl() noexcept {
        for (Slot& s : slots_) {
            advanceLanes(s);
            foldSourceRms(s);
            updateBreathGain(s);
            updateResonatorControl(s);
            updateCombControl(s);
            updateFilterControl(s);
        }
    }

    // =========================================================================
    // GranularDust (FR-030 .. FR-036, plan S6.1)
    // =========================================================================

    /// @brief The per-sample window advance of a grain born right now.
    /// 1 / (grain length in samples), so one full pass of the shared Hann table
    /// takes exactly dustGrainMsEffective. Read ONCE per grain, at birth: an
    /// in-flight grain keeps its own increment even if the length is rewritten
    /// under it, so no grain is ever truncated by a parameter change (FR-036).
    [[nodiscard]] float dustPhaseIncrement(const Slot& s) const noexcept {
        const float lengthSamples =
            s.dustGrainMsEffective * 0.001f * static_cast<float>(sampleRate_);
        // updateDustEffective floors the length at 5 ms, so lengthSamples cannot
        // reach zero at any legal sample rate; the max is belt-and-braces against
        // a pathological rate rather than an expected branch.
        return 1.0f / std::max(1.0f, lengthSamples);
    }

    /// @brief Find the pool slot the next grain should be born into.
    ///
    /// A FREE slot first, scanning forward from grainCursor so the pool is used
    /// round-robin rather than always reusing index 0. Only when all
    /// kMaxDustGrains are live does it STEAL, and the victim is the grain with
    /// the LARGEST phase - the one nearest the end of its own Hann window, and
    /// therefore the one whose envelope value is closest to zero. That makes the
    /// truncation step the smallest available.
    ///
    /// The alternative an unconditional ring cursor would give - overwrite
    /// whatever sits at the cursor - cuts a live Hann envelope at an arbitrary
    /// value, which is a step discontinuity in the source and exactly what
    /// SC-019 (b)'s envelope arm at the concurrency ceiling is watching for.
    ///
    /// No modulo: a division per grain birth is not worth it at 20 000 births a
    /// second, and the wrap is a single compare.
    [[nodiscard]] static DustGrain& acquireGrain(Slot& s) noexcept {
        for (std::size_t k = 0; k < kMaxDustGrains; ++k) {
            std::size_t index = s.grainCursor + k;
            if (index >= kMaxDustGrains) {
                index -= kMaxDustGrains;
            }
            if (!s.grains[index].active) {
                s.grainCursor = (index + 1 < kMaxDustGrains) ? index + 1 : 0;
                return s.grains[index];
            }
        }

        std::size_t victim     = 0;
        float       largestPhase = -1.0f;
        for (std::size_t k = 0; k < kMaxDustGrains; ++k) {
            if (s.grains[k].phase > largestPhase) {
                largestPhase = s.grains[k].phase;
                victim       = k;
            }
        }
        s.grainCursor = (victim + 1 < kMaxDustGrains) ? victim + 1 : 0;
        return s.grains[victim];
    }

    /// @brief Render one chunk of the GranularDust source into scratchA_.
    ///
    /// Trigger: the slot's own NoiseGenerator with ONLY NoiseType::Velvet enabled
    /// (pinned by effectiveNoiseType). Velvet emits exactly 0.0f between impulses
    /// and +/- velvetGain at them (noise_generator.h:545-556), so `!= 0.0f` is an
    /// EXACT impulse detector that consumes no extra RNG stream and needs no
    /// threshold - and the impulse's random polarity supplies each grain's sign,
    /// decorrelating overlapping grains for free.
    ///
    /// Carrier: the slot's NoiseOscillator at dustColor, rendered into scratchB_.
    /// Reusing scratchB_ is safe and deliberate: the comb stage, its only other
    /// user, has not run yet this chunk - so the model costs no third buffer.
    ///
    /// Continuity at birth and death is STRUCTURAL, not enforced: a Hann table
    /// starts and ends at exactly 0 (grain_envelope.h:46-51), so a grain fades in
    /// from silence and retires at silence with no ramp of its own.
    void renderDust(Slot& s, std::size_t n) noexcept {
        s.generator.process(scratchA_.data(), n);
        s.carrier.processBlock(scratchB_.data(), n);

        const float increment = dustPhaseIncrement(s);

        for (std::size_t i = 0; i < n; ++i) {
            const float trigger = scratchA_[i];
            if (trigger != 0.0f) {
                DustGrain& grain     = acquireGrain(s);
                grain.phase          = 0.0f;
                grain.phaseIncrement = increment;
                // FR-036: the birth gain is LOCKED for the grain's whole life,
                // alongside its increment. A density change mid-flight therefore
                // never rescales a sounding grain.
                grain.gain   = (trigger > 0.0f ? 1.0f : -1.0f) * s.dustGrainGain;
                grain.active = true;
            }

            float envelopeSum = 0.0f;
            for (DustGrain& grain : s.grains) {
                if (!grain.active) {
                    continue;
                }
                envelopeSum += GrainEnvelope::lookup(dustEnvelope_.data(),
                                                     kDustEnvelopeTableSize,
                                                     grain.phase) *
                               grain.gain;
                grain.phase += grain.phaseIncrement;
                if (grain.phase >= 1.0f) {
                    grain.active = false;
                    grain.phase  = 0.0f;
                }
            }

            scratchA_[i] = scratchB_[i] * envelopeSum;
        }
    }

    /// @brief Render at most kControlChunkSamples samples of the mix.
    ///
    /// Step 0 is unconditional and comes before any slot is considered: this is
    /// FR-003's overwrite contract, not a micro-optimisation. Under a
    /// "first slot writes with =" rule a dormant or dropped slot 0 would leave
    /// the caller's buffer untouched and the FR-074 tail would scale-and-clamp
    /// host garbage. The shipped idiom is identical
    /// (timevar_comb_bank.h:761-763).
    void renderChunk(float* out, std::size_t n) noexcept {
        std::fill_n(out, n, 0.0f);

        for (std::size_t slotIndex = 0; slotIndex < kMaxSources; ++slotIndex) {
            Slot& s = slots_[slotIndex];

            // ---- (1) source -> scratchA_ -----------------------------------
            // Runs for EVERY slot, dormant ones included (FR-071, Q6): the RNG
            // and the colour-filter state (brown's leaky integrator
            // noise_generator.h:467, pink's Kellet chain) must be exactly where
            // an always-awake slot's would be when the slot wakes.
            //
            // Direct, FilteredWind and MetallicHiss are all exactly this call:
            // their whole difference is which type effectiveNoiseType() pinned
            // and how the chain downstream is configured (plan S5.3).
            // GranularDust is the one model that is more than configuration - it
            // windows a carrier with a pool of grains the velvet train triggers
            // (plan S6.1) - and it leaves its result in the same scratchA_, so
            // everything downstream of here is model-agnostic.
            if (s.model == NoiseOrganismModel::GranularDust) {
                renderDust(s, n);
            } else {
                s.generator.process(scratchA_.data(), n);
            }

            // ---- (1b) source RMS, on THIS slot's own data ------------------
            // Here, never in updateControl(): that routine runs BEFORE
            // renderChunk for the step, and scratchA_ is one organism-level
            // buffer reused by every slot in sequence, so reading it there would
            // hand every slot but the last a different slot's level (plan S5.6).
            float sumSquares = 0.0f;
            for (std::size_t i = 0; i < n; ++i) {
                const float sample = scratchA_[i];
                sumSquares += sample * sample;
            }
            s.sourceSumSq += sumSquares;
            s.sourceSumCount += n;

            if (!chainActive(s)) {
                // Dormant / dropped: chain skipped, contributes nothing. The
                // gain ramps are still advanced sample-for-sample so their state
                // stays a pure function of the sample count and the render stays
                // block-size invariant (SC-016) - and so a duck armed while the
                // gate already sits at zero still reaches its swap point.
                for (std::size_t i = 0; i < n; ++i) {
                    (void)advanceSlotGain(slotIndex);
                }
                continue;
            }

            // ---- (2) resonators, in place ----------------------------------
            // At 0 enabled the call is SKIPPED, never forwarded: with nothing
            // enabled process() returns input*mix + wetSum*(1-mix) with
            // wetSum == 0 and exciterMix_ == 0 (resonator_bank.h:511, :589),
            // i.e. SILENCE, not bypass (FR-051).
            if (s.numResonators > 0) {
                s.resonators.processBlock(scratchA_.data(), n);
            }

            // ---- (3) combs -------------------------------------------------
            // Likewise skipped rather than forwarded at 0: setNumCombs floors at
            // 1 (timevar_comb_bank.h:502) and process() has no dry path, so a
            // forwarded 0 would leave one comb running (FR-054).
            if (s.numCombs > 0) {
                s.combs.processBlock(scratchA_.data(), scratchB_.data(), n);
            } else {
                std::copy_n(scratchA_.data(), n, scratchB_.data());
            }

            // ---- (4) chain filter, in place --------------------------------
            s.filter.processBlock(scratchB_.data(), n);

            // ---- (5) gain and mix ------------------------------------------
            // modelTrimGain and breathGain are constant across the chunk;
            // levelRamp and gate advance per sample (FR-073, plan S8.1).
            //
            // modelTrimGain is deliberately OUTSIDE advanceSlotGain (and so
            // outside getSourceGain): it is a calibration property of the source,
            // not a gain the caller set, and kModelTrimDb[Direct] == 0 by
            // construction is what keeps SC-018's "never leaves 1.0" arm exact.
            //
            // Hoisted before the loop even though a duck swap inside it can change
            // the model: for the tail of that one chunk the trim (and scratchB_'s
            // already-rendered source) are the OLD model's. That is at most 63
            // samples at a gate that has just left zero - under 5 % of full gain -
            // and the alternative, re-reading the trim per sample, would put a
            // table lookup and a dbToGain on every sample of every slot.
            const float trimGain = modelTrimGain(s.model);
            for (std::size_t i = 0; i < n; ++i) {
                out[i] += scratchB_[i] * advanceSlotGain(slotIndex) * trimGain;
            }
        }

        // ---- FR-074 tail: scale, clamp, count, flush -----------------------
        std::uint32_t engagements = 0;
        for (std::size_t i = 0; i < n; ++i) {
            float sample = out[i] * kMixScale;
            if (sample > kOutputClamp) {
                sample = kOutputClamp;
                ++engagements;
            } else if (sample < -kOutputClamp) {
                sample = -kOutputClamp;
                ++engagements;
            }
            out[i] = detail::flushDenormal(sample);
        }
        // Saturating, never wrapping: the count is a diagnostic and a wrap would
        // let a pathological render report zero engagements (FR-074).
        if (engagements > 0) {
            const std::uint32_t headroom = kMaxClampCount - clampEngagements_;
            clampEngagements_ += std::min(engagements, headroom);
        }
    }

    // =========================================================================
    // Organism-level state
    // =========================================================================
    double        sampleRate_ = 48000.0;
    PrepareConfig config_{};
    bool          prepared_ = false;
    /// 0 is legal - deriveStreamSeed never yields a degenerate stream (random.h:112).
    std::uint32_t seed_          = 0;
    bool          wanderEnabled_ = true;
    float         wanderRateHz_  = kDefaultWanderRateHz;
    /// ABSOLUTE 64-sample control grid residue (plan S5.1). Deliberately not the
    /// block-relative counter HarmonicCloud uses: that would run two control
    /// steps for a 36+28 split where an unsplit 64 runs one, breaking SC-016.
    std::size_t   controlPhase_     = 0;
    std::uint32_t clampEngagements_ = 0;
    std::size_t   allocatedBytes_   = 0;
    /// Length of every FR-013/FR-073 gain ramp, in samples (plan S2.1 step 2).
    std::size_t   rampSamples_      = 1;

    std::array<float, kDustEnvelopeTableSize> dustEnvelope_{};
    /// Two shared 64-sample scratch buffers: slots render sequentially, so no
    /// per-slot buffers are needed.
    std::array<float, kControlChunkSamples> scratchA_{};
    std::array<float, kControlChunkSamples> scratchB_{};

    std::array<Slot, kMaxSources> slots_{};
};

} // namespace DSP
} // namespace Krate
