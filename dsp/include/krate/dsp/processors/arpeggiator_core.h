// ==============================================================================
// Layer 2: Processor - ArpeggiatorCore (Timing & Event Generation)
// ==============================================================================
// Composes HeldNoteBuffer + NoteSelector (Layer 1) with integer sample-accurate
// timing to produce ArpEvent sequences. Header-only, zero heap allocation in
// all methods.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocation, no locks, no IO)
// - Principle III: Modern C++ (C++20, enum class, constexpr, std::span)
// - Principle IX: Layer 2 (depends on Layer 0: BlockContext, NoteValue;
//                          Layer 1: HeldNoteBuffer, NoteSelector)
//
// Reference: specs/070-arpeggiator-core/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/chord_generator.h>
#include <krate/dsp/core/euclidean_pattern.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/core/scale_harmonizer.h>
#include <krate/dsp/core/transport_sync.h>
#include <krate/dsp/primitives/arp_lane.h>
#include <krate/dsp/primitives/held_note_buffer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Krate::DSP {

// =============================================================================
// ArpStepFlags (073-per-step-mods, FR-001)
// =============================================================================

/// @brief Per-step modifier flags stored as bitmask in modifier lane.
/// Multiple flags can be combined on a single step.
/// If kStepActive is not set, the step is a rest (silence).
enum ArpStepFlags : uint8_t {
    kStepActive = 0x01,   ///< Note fires. Off = Rest.
    kStepTie    = 0x02,   ///< Sustain previous note, no retrigger
    kStepSlide  = 0x04,   ///< Legato noteOn, suppress previous noteOff, portamento
    kStepAccent = 0x08,   ///< Velocity boost by accentVelocity_ amount
};

// =============================================================================
// Enumerations (FR-027, FR-028)
// =============================================================================

/// @brief How the arp handles key release.
enum class LatchMode : uint8_t {
    Off = 0,  ///< Stop when all keys released
    Hold,     ///< Continue playing latched pattern; new keys replace
    Add       ///< Accumulate notes into pattern
};

/// @brief When the arp pattern resets.
/// Named ArpRetriggerMode to avoid ODR conflict with RetriggerMode in envelope_utils.h.
enum class ArpRetriggerMode : uint8_t {
    Off = 0,  ///< Never auto-reset
    Note,     ///< Reset on each incoming noteOn
    Beat      ///< Reset at bar boundaries
};

/// @brief Note-source mode (spec 142, Gradus Sequencer Note lane).
/// Gradus selects between live held-note MIDI input (Live) and the programmed
/// Sequencer Note pattern (Sequencer). Ruinae never uses anything but Live —
/// lane 10 is conditionally inert in Live mode so Ruinae is unaffected.
enum class SourceMode : uint8_t {
    Live = 0,   ///< Live held-note MIDI input drives the arp pipeline (default).
    Sequencer   ///< Programmed Sequencer Note lane pattern drives the pipeline.
};

// =============================================================================
// TrigCondition (076-conditional-trigs, FR-001, FR-002)
// =============================================================================

/// @brief Conditional trigger type for per-step condition evaluation.
/// Each arp step has exactly one condition (not a bitmask).
enum class TrigCondition : uint8_t {
    Always = 0,       ///< Step fires unconditionally (default)
    Prob10,           ///< ~10% probability
    Prob25,           ///< ~25% probability
    Prob50,           ///< ~50% probability
    Prob75,           ///< ~75% probability
    Prob90,           ///< ~90% probability
    Ratio_1_2,        ///< Fire on 1st of every 2 loops
    Ratio_2_2,        ///< Fire on 2nd of every 2 loops
    Ratio_1_3,        ///< Fire on 1st of every 3 loops
    Ratio_2_3,        ///< Fire on 2nd of every 3 loops
    Ratio_3_3,        ///< Fire on 3rd of every 3 loops
    Ratio_1_4,        ///< Fire on 1st of every 4 loops
    Ratio_2_4,        ///< Fire on 2nd of every 4 loops
    Ratio_3_4,        ///< Fire on 3rd of every 4 loops
    Ratio_4_4,        ///< Fire on 4th of every 4 loops
    First,            ///< Fire only on first loop (loopCount == 0)
    Fill,             ///< Fire only when fill mode is active
    NotFill,          ///< Fire only when fill mode is NOT active
    kCount            ///< Sentinel (18). Not a valid condition.
};

// =============================================================================
// ArpEvent (FR-001, FR-002)
// =============================================================================

/// @brief A timestamped MIDI event generated by the arpeggiator.
struct ArpEvent {
    enum class Type : uint8_t { NoteOn, NoteOff, kSkip };

    Type type{Type::NoteOn};
    uint8_t note{0};          ///< MIDI note number (0-127), or step index for kSkip
    uint8_t velocity{0};      ///< MIDI velocity (0-127)
    int32_t sampleOffset{0};  ///< Sample position within block [0, blockSize-1]
    bool legato{false};       ///< When true: suppress envelope retrigger, apply portamento
};

// =============================================================================
// ArpeggiatorCore (FR-003 through FR-032)
// =============================================================================

/// @brief Layer 2 DSP processor: arpeggiator timing and event generation.
///
/// Composes HeldNoteBuffer + NoteSelector (Layer 1) with integer timing
/// to produce sample-accurate ArpEvent sequences. Header-only, zero heap
/// allocation in all methods.
///
/// @par Real-Time Safety
/// All methods are noexcept. Zero heap allocation. No locks, exceptions, or I/O.
///
/// @par Usage
/// @code
/// ArpeggiatorCore arp;
/// arp.prepare(44100.0, 512);
/// arp.setEnabled(true);
/// arp.setMode(ArpMode::Up);
/// arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
/// arp.noteOn(60, 100);
/// arp.noteOn(64, 100);
///
/// std::array<ArpEvent, 128> events;
/// BlockContext ctx;
/// ctx.isPlaying = true;
/// size_t count = arp.processBlock(ctx, events);
/// @endcode
class ArpeggiatorCore {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxEvents = 128;
    /// Scheduled NoteOffs in flight. Must cover the worst case the parameter
    /// ranges permit, or addPendingNoteOff starts evicting: a full 32-note
    /// chord (HeldNoteBuffer and ArpNoteResult both cap at 32) sustained at up
    /// to kMaxGateLength = 200% overlaps itself across two steps, so 2 x 32.
    /// At 32 the ring overflowed on every step of a big sustained chord, and
    /// each eviction fired that note's NoteOff early at sampleOffset 0 --
    /// cutting notes short and emitting events out of time order.
    static constexpr size_t kMaxPendingNoteOffs = 64;
    /// 0=velocity, 1=gate, 2=pitch, 3=modifier, 4=ratchet, 5=condition,
    /// 6=chord, 7=inversion, 8=MIDI delay, 9=sequencer note (spec 142, lane 10).
    /// Lane 9 is conditionally inert in Live mode (`sourceMode_ == Live`).
    static constexpr size_t kNumLanes = 10;
    static constexpr double kMinSampleRate = 1000.0;
    static constexpr float kMinFreeRate = 0.5f;
    static constexpr float kMaxFreeRate = 50.0f;
    static constexpr float kMinGateLength = 1.0f;
    static constexpr float kMaxGateLength = 200.0f;
    static constexpr float kMinSwing = 0.0f;
    static constexpr float kMaxSwing = 75.0f;

    // =========================================================================
    // Construction (072-independent-lanes: lane defaults for SC-002)
    // =========================================================================

    ArpeggiatorCore() noexcept {
        // Set velocity lane default: length=1, step[0]=1.0f (full passthrough)
        // This ensures SC-002 bit-identical backward compat from first use.
        velocityLane_.setStep(0, 1.0f);
        // Set gate lane default: length=1, step[0]=1.0f (pure global gate)
        gateLane_.setStep(0, 1.0f);
        // Set modifier lane default: length=1, step[0]=kStepActive (0x01)
        // ArpLane<uint8_t> zero-initializes steps to 0x00 = Rest. Without this
        // call, the default modifier lane would silence every arp note.
        modifierLane_.setStep(0, static_cast<uint8_t>(kStepActive));
        // Set ratchet lane default: length=1, step[0]=1 (no ratcheting)
        // ArpLane<uint8_t> zero-initializes steps to 0, which for ratchet
        // would mean count 0 (invalid). Must explicitly set to 1 (FR-003).
        ratchetLane_.setStep(0, static_cast<uint8_t>(1));

        // 075-euclidean-timing: initialize pattern from defaults (FR-001)
        // Without this, euclideanPattern_ would be 0 (the member initializer)
        // instead of the correct E(4,8,0) bitmask.
        regenerateEuclideanPattern();

        // 076-conditional-trigs: initialize condition lane default (FR-005)
        // ArpLane<uint8_t> zero-initializes to 0 = TrigCondition::Always,
        // but explicit set for clarity and consistency.
        conditionLane_.setStep(0, static_cast<uint8_t>(TrigCondition::Always));

        // arp-chord-lane: initialize chord/inversion lane defaults
        // ChordType::None (0) and InversionType::Root (0) = single note behavior.
        // ArpLane<uint8_t> zero-initializes to 0, which is correct for both.

        // 077-spice-dice-humanize: initialize overlay arrays to identity (FR-002)
        // velocity = 1.0 (full passthrough), gate = 1.0 (full passthrough),
        // ratchet = 1 (no subdivision), condition = 0 (Always)
        velocityOverlay_.fill(1.0f);
        gateOverlay_.fill(1.0f);
        ratchetOverlay_.fill(1);
        conditionOverlay_.fill(static_cast<uint8_t>(TrigCondition::Always));

        // 084-arp-scale-mode: default to Chromatic for backward compatibility (FR-004)
        // ScaleHarmonizer defaults to Major, but the arp must default to Chromatic
        // so existing presets/behavior are unchanged.
        scaleHarmonizer_.setScale(ScaleType::Chromatic);

        // Speed-curve triple buffer: the three slot indices must start DISTINCT
        // (ready=0, writer=1, reader=2) or the writer and the audio thread would
        // begin life aliasing the same staging buffer. Value-initialization
        // would give all three 0.
        laneSpeedCurveWriteSlot_.fill(uint8_t{1});
        laneSpeedCurveReadSlot_.fill(uint8_t{2});
        for (auto& ready : laneSpeedCurveReady_)
            ready.store(uint8_t{0}, std::memory_order_relaxed);

        // Spec 142: Sequencer Note lane defaults. Pitch step[0]=60 (C4), all
        // rest flags=1 (rest) so a fresh pattern is silent until user populates.
        // Expand to full capacity first: ArpLane::setStep clamps its index to
        // length-1, so filling at length 16 would collapse steps 16-31 onto
        // index 15 and leave them at 0 once the lane is later expanded.
        seqNoteLane_.setLength(32);
        for (size_t i = 0; i < 32; ++i) {
            seqNoteLane_.setStep(i, static_cast<uint8_t>(60));
            seqRestFlags_[i].store(1, std::memory_order_relaxed);
        }
        seqNoteLane_.setLength(16);
    }

    // =========================================================================
    // Lifecycle (FR-003, FR-004)
    // =========================================================================

    /// @brief Initialize for processing.
    /// @param sampleRate Sample rate in Hz (clamped to >= 1000)
    /// @param maxBlockSize Maximum block size (for validation)
    inline void prepare(double sampleRate,
                        [[maybe_unused]] size_t maxBlockSize) noexcept {
        sampleRate_ = (sampleRate >= kMinSampleRate) ? sampleRate : kMinSampleRate;
        reset();
    }

    /// @brief Like `prepare()`, but carries an outstanding panic note-off
    /// obligation across the reset.
    ///
    /// Hosts reconfigure with setActive(false) -> setupProcessing() ->
    /// setActive(true). A plain `prepare()` in the middle of that sequence
    /// clears `panicRequested_` *and* the `currentArpNotes_` list the panic
    /// would have released, so notes already emitted to a MIDI *output* bus are
    /// stranded downstream. Call this instead whenever a deactivation flush is
    /// still outstanding: all timing state is reset for the new sample rate, but
    /// the note-off obligation survives to discharge on the next `processBlock`.
    inline void prepareRetainingPanicNoteOff(double sampleRate,
                                             size_t maxBlockSize) noexcept {
        const bool panic = panicRequested_;
        const bool disableNoteOff = needsDisableNoteOff_;
        const auto savedNotes = currentArpNotes_;
        const size_t savedCount = currentArpNoteCount_;

        prepare(sampleRate, maxBlockSize);

        if (panic) {
            currentArpNotes_ = savedNotes;
            currentArpNoteCount_ = savedCount;
            panicRequested_ = true;
            needsDisableNoteOff_ = disableNoteOff;
        }
    }

    /// @brief Reset all state to initial values. Configuration preserved.
    inline void reset() noexcept {
        sampleCounter_ = 0;
        currentStepDuration_ = 0;
        swingStepCounter_ = 0;
        wasPlaying_ = false;
        firstStepPending_ = true;
        transportLoopPending_ = false;
        currentArpNoteCount_ = 0;
        pendingNoteOffCount_ = 0;
        needsDisableNoteOff_ = false;
        panicRequested_ = false;
        physicalKeysHeld_ = 0;
        latchActive_ = false;
        selector_.reset();
        heldNotes_.clear();
        resetLanes();
        regenerateEuclideanPattern();  // 075-euclidean-timing: regenerate from current params (FR-014)
    }

    // =========================================================================
    // MIDI Input (FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Handle incoming MIDI note-on.
    /// Applies latch logic (replace vs accumulate) and retrigger logic.
    inline void noteOn(uint8_t note, uint8_t velocity) noexcept {
        ++physicalKeysHeld_;

        // Latch Hold: if currently latched (all keys were released), clear old
        // pattern and start fresh. This replaces the entire latched pattern.
        // Spec 142 FR-022: Latch is inert in Sequencer mode — skip.
        if (sourceMode_ != SourceMode::Sequencer
            && latchMode_ == LatchMode::Hold && latchActive_) {
            heldNotes_.clear();
            latchActive_ = false;
        }
        // Latch Add: always add, never clear (pattern accumulates)
        // Latch Off: standard behavior (just add)

        // Scale quantize input (FR-009): snap note to nearest scale note
        // before entering the held notes buffer.
        // Spec 142 FR-022: ScaleQuantizeInput is inert in Sequencer mode —
        // the pattern is the source, not held-input quantize.
        uint8_t effectiveNote = note;
        if (scaleQuantizeInput_
            && sourceMode_ != SourceMode::Sequencer
            && scaleHarmonizer_.getScale() != ScaleType::Chromatic) {
            effectiveNote = static_cast<uint8_t>(
                std::clamp(scaleHarmonizer_.quantizeToScale(static_cast<int>(note)), 0, 127));
        }

        heldNotes_.noteOn(effectiveNote, velocity);

        if (retriggerMode_ == ArpRetriggerMode::Note) {
            selector_.reset();
            swingStepCounter_ = 0;
            resetLanes();
        }
    }

    /// @brief Handle incoming MIDI note-off.
    /// Behavior depends on latch mode (remove, ignore, or track).
    /// Spec 142 (T030e, FR-022): in Sequencer mode the Latch parameter is
    /// inert — the transposition root must revert on physical key release.
    /// We bypass the switch entirely and always remove the note.
    inline void noteOff(uint8_t note) noexcept {
        if (physicalKeysHeld_ > 0) {
            --physicalKeysHeld_;
        }

        if (sourceMode_ == SourceMode::Sequencer) {
            heldNotes_.noteOff(note);
            return;
        }

        switch (latchMode_) {
            case LatchMode::Off:
                // Remove from held buffer
                heldNotes_.noteOff(note);
                // If buffer empty, signal that current arp note needs NoteOff
                if (heldNotes_.empty()) {
                    needsDisableNoteOff_ = true;
                }
                break;

            case LatchMode::Hold:
                // Do NOT remove from heldNotes_ -- notes remain for latched pattern.
                // When all physical keys are released, activate latch.
                if (physicalKeysHeld_ == 0) {
                    latchActive_ = true;
                }
                break;

            case LatchMode::Add:
                // Do NOT remove from heldNotes_ -- pattern accumulates indefinitely.
                break;
        }
    }

    // =========================================================================
    // Configuration (FR-008 through FR-018)
    // =========================================================================

    /// @brief Enable or disable the arpeggiator (FR-008, FR-022).
    /// Disable->enable transition resets all lane positions to step 0.
    inline void setEnabled(bool enabled) noexcept {
        if (enabled_ && !enabled) {
            needsDisableNoteOff_ = true;
            ratchetSubStepsRemaining_ = 0;  // 074-ratcheting: clear pending sub-steps (FR-026)
            ratchetSubStepCounter_ = 0;
            ratchetSubStepIndex_ = 0;
        }
        if (!enabled_ && enabled) {
            // FR-022: disable/enable transition resets lanes
            resetLanes();
        }
        enabled_ = enabled;
    }

    /// @brief Set arp mode, delegating to NoteSelector (FR-009).
    /// Also resets swingStepCounter_ to 0 so next step gets even timing.
    inline void setMode(ArpMode mode) noexcept {
        arpMode_ = mode;
        selector_.setMode(mode);  // setMode() calls reset() internally
        swingStepCounter_ = 0;
    }

    /// @brief Set octave range 1-4, delegating to NoteSelector (FR-010).
    inline void setOctaveRange(int octaves) noexcept {
        selector_.setOctaveRange(std::clamp(octaves, 1, 4));
    }

    /// @brief Set octave traversal mode, delegating to NoteSelector (FR-011).
    inline void setOctaveMode(OctaveMode mode) noexcept {
        selector_.setOctaveMode(mode);
    }

    /// @brief Toggle tempo sync vs free rate mode (FR-012).
    inline void setTempoSync(bool sync) noexcept {
        tempoSync_ = sync;
    }

    /// @brief Set tempo-synced step rate (FR-013).
    inline void setNoteValue(NoteValue val, NoteModifier mod) noexcept {
        noteValue_ = val;
        noteModifier_ = mod;
    }

    /// @brief Set free-running rate in Hz, clamped 0.5-50.0 (FR-014).
    inline void setFreeRate(float hz) noexcept {
        freeRateHz_ = std::clamp(hz, kMinFreeRate, kMaxFreeRate);
    }

    /// @brief Set gate length as percentage 1-200% (FR-015).
    inline void setGateLength(float percent) noexcept {
        gateLengthPercent_ = std::clamp(percent, kMinGateLength, kMaxGateLength);
    }

    /// @brief Set swing as percentage 0-75% (FR-016).
    /// Stored internally as 0.0-0.75 (divided by 100).
    inline void setSwing(float percent) noexcept {
        float clamped = std::clamp(percent, kMinSwing, kMaxSwing);
        swing_ = clamped / 100.0f;
    }

    /// @brief Set latch mode (FR-017).
    inline void setLatchMode(LatchMode mode) noexcept {
        latchMode_ = mode;
    }

    /// @brief Set retrigger mode (FR-018).
    inline void setRetrigger(ArpRetriggerMode mode) noexcept {
        retriggerMode_ = mode;
    }

    // =========================================================================
    // Lane Accessors (072-independent-lanes, FR-010 through FR-024)
    // =========================================================================

    /// @brief Access velocity lane for configuration.
    ArpLane<float>& velocityLane() noexcept { return velocityLane_; }

    /// @brief Access velocity lane (const).
    [[nodiscard]] const ArpLane<float>& velocityLane() const noexcept {
        return velocityLane_;
    }

    /// @brief Access gate lane for configuration.
    ArpLane<float>& gateLane() noexcept { return gateLane_; }

    /// @brief Access gate lane (const).
    [[nodiscard]] const ArpLane<float>& gateLane() const noexcept {
        return gateLane_;
    }

    /// @brief Access pitch lane for configuration.
    ArpLane<int8_t>& pitchLane() noexcept { return pitchLane_; }

    /// @brief Access pitch lane (const).
    [[nodiscard]] const ArpLane<int8_t>& pitchLane() const noexcept {
        return pitchLane_;
    }

    // =========================================================================
    // Modifier Lane Accessors (073-per-step-mods, FR-024)
    // =========================================================================

    /// @brief Access the modifier lane for reading/writing step values.
    ArpLane<uint8_t>& modifierLane() noexcept { return modifierLane_; }

    /// @brief Const access to the modifier lane.
    [[nodiscard]] const ArpLane<uint8_t>& modifierLane() const noexcept {
        return modifierLane_;
    }

    // =========================================================================
    // Ratchet Lane Accessors (074-ratcheting, FR-006)
    // =========================================================================

    /// @brief Access the ratchet lane for reading/writing step values.
    ArpLane<uint8_t>& ratchetLane() noexcept { return ratchetLane_; }

    /// @brief Const access to the ratchet lane.
    [[nodiscard]] const ArpLane<uint8_t>& ratchetLane() const noexcept {
        return ratchetLane_;
    }

    // =========================================================================
    // Euclidean Timing Setters (075-euclidean-timing, FR-009, FR-010)
    // =========================================================================

    /// @brief Set the number of steps in the Euclidean pattern.
    /// Clamps to [kMinSteps (2), kMaxSteps (32)].
    /// Also re-clamps euclideanHits_ to [0, new step count].
    /// Regenerates the pattern bitmask.
    inline void setEuclideanSteps(int steps) noexcept {
        euclideanSteps_ = std::clamp(steps,
            EuclideanPattern::kMinSteps, EuclideanPattern::kMaxSteps);
        // Re-clamp hits against new step count
        euclideanHits_ = std::clamp(euclideanHits_, 0, euclideanSteps_);
        regenerateEuclideanPattern();
    }

    /// @brief Set the number of hit pulses in the Euclidean pattern.
    /// Clamps to [0, euclideanSteps_].
    /// Regenerates the pattern bitmask.
    inline void setEuclideanHits(int hits) noexcept {
        euclideanHits_ = std::clamp(hits, 0, euclideanSteps_);
        regenerateEuclideanPattern();
    }

    /// @brief Set the rotation offset for the Euclidean pattern.
    /// Clamps to [0, kMaxSteps - 1 (31)].
    /// Regenerates the pattern bitmask.
    inline void setEuclideanRotation(int rotation) noexcept {
        euclideanRotation_ = std::clamp(rotation, 0,
            EuclideanPattern::kMaxSteps - 1);
        regenerateEuclideanPattern();
    }

    /// @brief Enable or disable Euclidean timing mode.
    /// When transitioning from disabled to enabled, resets euclideanPosition_ to 0.
    /// Does NOT clear ratchet sub-step state (in-flight sub-steps complete normally).
    inline void setEuclideanEnabled(bool enabled) noexcept {
        if (!euclideanEnabled_ && enabled) {
            // Transitioning from disabled to enabled: reset position (FR-010)
            euclideanPosition_ = 0;
            // Do NOT clear ratchet sub-step state -- in-flight sub-steps complete
        }
        euclideanEnabled_ = enabled;
    }

    // =========================================================================
    // Euclidean Timing Getters (075-euclidean-timing, FR-015)
    // =========================================================================

    /// @brief Check if Euclidean timing mode is enabled.
    [[nodiscard]] inline bool euclideanEnabled() const noexcept {
        return euclideanEnabled_;
    }

    /// @brief Get the number of hit pulses in the Euclidean pattern.
    [[nodiscard]] inline int euclideanHits() const noexcept {
        return euclideanHits_;
    }

    /// @brief Get the number of steps in the Euclidean pattern.
    [[nodiscard]] inline int euclideanSteps() const noexcept {
        return euclideanSteps_;
    }

    /// @brief Get the rotation offset for the Euclidean pattern.
    [[nodiscard]] inline int euclideanRotation() const noexcept {
        return euclideanRotation_;
    }

    // =========================================================================
    // Condition Lane Accessors (076-conditional-trigs, FR-007)
    // =========================================================================

    /// @brief Access the condition lane for reading/writing step values.
    ArpLane<uint8_t>& conditionLane() noexcept { return conditionLane_; }

    /// @brief Const access to the condition lane.
    [[nodiscard]] const ArpLane<uint8_t>& conditionLane() const noexcept {
        return conditionLane_;
    }

    // =========================================================================
    // Chord Lane Accessors (arp-chord-lane)
    // =========================================================================

    /// @brief Access the chord type lane for reading/writing step values.
    ArpLane<uint8_t>& chordLane() noexcept { return chordLane_; }

    /// @brief Const access to the chord type lane.
    [[nodiscard]] const ArpLane<uint8_t>& chordLane() const noexcept {
        return chordLane_;
    }

    /// @brief Access the inversion lane for reading/writing step values.
    ArpLane<uint8_t>& inversionLane() noexcept { return inversionLane_; }

    /// @brief Const access to the inversion lane.
    [[nodiscard]] const ArpLane<uint8_t>& inversionLane() const noexcept {
        return inversionLane_;
    }

    // =========================================================================
    // MIDI Delay Lane Accessor
    // =========================================================================

    /// @brief Access the MIDI delay step-tracking lane.
    /// The delay lane participates in the same polymetric advancement as the
    /// other 8 lanes — it advances in fireStep with its own speed/swing/jitter.
    /// The actual echo scheduling is handled by the external MidiNoteDelay
    /// post-processor, which reads currentStep() from this lane.
    ArpLane<uint8_t>& midiDelayLane() noexcept { return midiDelayLane_; }

    [[nodiscard]] const ArpLane<uint8_t>& midiDelayLane() const noexcept {
        return midiDelayLane_;
    }

    // =========================================================================
    // Sequencer Note Lane Accessor (spec 142, lane index 9)
    // =========================================================================

    /// @brief Access the Sequencer Note lane (lane 10 — index 9).
    /// Stores per-step MIDI pitches (uint8_t, 0-127). Pairs with
    /// `seqRestFlags()` (separate atomic array) per step. The lane is
    /// conditionally inert in Live mode — `fireStep` skips advance + emission
    /// when `sourceMode_ == SourceMode::Live`.
    ArpLane<uint8_t>& seqNoteLane() noexcept { return seqNoteLane_; }
    [[nodiscard]] const ArpLane<uint8_t>& seqNoteLane() const noexcept {
        return seqNoteLane_;
    }

    /// @brief Access the per-step rest flags for the Sequencer Note lane.
    /// rest=1 suppresses note-on for that step; the playhead still advances
    /// (FR-019). Stored as `std::atomic<uint8_t>` so the controller's UI
    /// thread can write the flags and the audio thread reads them.
    std::array<std::atomic<uint8_t>, 32>& seqRestFlags() noexcept {
        return seqRestFlags_;
    }
    [[nodiscard]] const std::array<std::atomic<uint8_t>, 32>&
    seqRestFlags() const noexcept { return seqRestFlags_; }

    /// @brief Set the note-source mode (Live vs Sequencer). Spec 142.
    /// Lane playheads are NOT reset on mode change (per spec Q5-A); held
    /// notes also remain in `heldNotes_` (single source of truth).
    void setSourceMode(SourceMode mode) noexcept { sourceMode_ = mode; }

    /// @brief Get the current note-source mode.
    [[nodiscard]] SourceMode sourceMode() const noexcept { return sourceMode_; }

    /// @brief Request a panic note-off for any currently sounding arp note.
    /// On the next `processBlock`, all notes in `currentArpNotes_` (plus any
    /// still-pending NoteOffs) emit at sample offset 0. Lane playheads are NOT
    /// touched. Spec 142 uses this on source-mode toggle, and Gradus uses it on
    /// deactivation, to avoid stuck notes.
    ///
    /// @note This discharges unconditionally at the top of the next
    /// processBlock -- regardless of enabled state, transport, source mode, or
    /// whether keys are still held. `needsDisableNoteOff_` alone is only
    /// consumed on the disabled and empty-held-buffer paths, so a panic
    /// requested while a chord was still held would otherwise never fire.
    void requestPanicNoteOff() noexcept {
        needsDisableNoteOff_ = true;
        panicRequested_ = true;
    }

    // =========================================================================
    // HeldNoteBuffer Accessor (read-only; spec 142 transposition root needs it)
    // =========================================================================

    /// @brief Read-only access to the held-note buffer.
    /// Used by tests (and downstream code) to introspect the most-recently-held
    /// note for transposition-root determination in Sequencer mode.
    [[nodiscard]] const HeldNoteBuffer& heldNotes() const noexcept {
        return heldNotes_;
    }

    /// @brief Set the global voicing mode.
    void setVoicingMode(VoicingMode mode) noexcept { voicingMode_ = mode; }

    /// @brief Get the current voicing mode.
    [[nodiscard]] VoicingMode voicingMode() const noexcept { return voicingMode_; }

    /// @brief Set per-lane speed multiplier.
    /// Lane indices: 0=velocity, 1=gate, 2=pitch, 3=modifier, 4=ratchet,
    ///               5=condition, 6=chord, 7=inversion
    void setLaneSpeed(size_t laneIndex, float speed) noexcept {
        if (laneIndex < kNumLanes)
            laneSpeedMultipliers_[laneIndex] = std::clamp(speed, 0.25f, 4.0f);
    }

    /// @brief Stage a baked speed curve lookup table for a lane.
    /// Safe to call from the message thread while the audio thread runs: the
    /// table is written into the staging slot the audio thread is NOT reading,
    /// and only then published via an atomic release.
    /// @param laneIndex Lane 0-7
    /// @param table 256-entry table with values in [0, 1] (0.5 = center)
    void setLaneSpeedCurveTable(size_t laneIndex,
                                const std::array<float, 256>& table) noexcept {
        if (laneIndex >= kNumLanes) return;
        // Triple-buffer publish. Fill the slot this thread owns, then swap it
        // into `ready` and take whatever was there as the next write target.
        // The reader owns its own slot for as long as it is copying, so writer
        // and reader can never touch the same buffer.
        uint8_t& writeSlot = laneSpeedCurveWriteSlot_[laneIndex];
        laneSpeedCurveTablesStaging_[laneIndex][writeSlot] = table;
        const uint8_t prev = laneSpeedCurveReady_[laneIndex].exchange(
            static_cast<uint8_t>(writeSlot | kCurveDirtyBit),
            std::memory_order_acq_rel);
        writeSlot = static_cast<uint8_t>(prev & kCurveSlotMask);
    }

    /// @brief Consume pending curve table updates. Call from audio thread only
    /// (e.g., at the start of process()).
    /// @note Spec 142 (T029): iteration upper bound corrected from a hardcoded
    /// `8` to `kNumLanes` — the previous loop silently skipped lanes 8+ (MIDI
    /// delay, sequencer note), so their curve tables would never be activated
    /// even when staged from the controller.
    void consumePendingCurveTables() noexcept {
        for (size_t i = 0; i < kNumLanes; ++i) {
            if ((laneSpeedCurveReady_[i].load(std::memory_order_acquire)
                 & kCurveDirtyBit) == 0)
                continue;
            // Hand our old slot back and take ownership of the published one.
            // The exchange clears the dirty bit (our slot index carries none),
            // and the writer's next target becomes the slot we just released --
            // so the buffer we are about to copy stays ours for the whole copy.
            uint8_t& readSlot = laneSpeedCurveReadSlot_[i];
            const uint8_t prev = laneSpeedCurveReady_[i].exchange(
                readSlot, std::memory_order_acq_rel);
            readSlot = static_cast<uint8_t>(prev & kCurveSlotMask);
            laneSpeedCurveTables_[i] = laneSpeedCurveTablesStaging_[i][readSlot];
        }
    }

    /// @brief Set speed curve depth for a lane (0 = off, 1 = full range).
    void setLaneSpeedCurveDepth(size_t laneIndex, float depth) noexcept {
        if (laneIndex < kNumLanes)
            laneSpeedCurveDepths_[laneIndex].store(std::clamp(depth, 0.0f, 1.0f),
                                                  std::memory_order_relaxed);
    }

    /// @brief Enable/disable speed curve for a lane. Thread-safe (atomic store).
    void setLaneSpeedCurveEnabled(size_t laneIndex, bool enabled) noexcept {
        if (laneIndex < kNumLanes)
            laneSpeedCurveEnabled_[laneIndex].store(enabled, std::memory_order_relaxed);
    }

    // =========================================================================
    // Per-Lane Modulator Accessors (spec 142 T029 — round-trip introspection)
    // =========================================================================

    /// @brief Read per-lane speed multiplier (returns 1.0 if out of bounds).
    [[nodiscard]] float laneSpeed(size_t laneIndex) const noexcept {
        return laneIndex < kNumLanes ? laneSpeedMultipliers_[laneIndex] : 1.0f;
    }

    /// @brief Read per-lane swing as a 0..1 fraction (returns 0 if OOB).
    [[nodiscard]] float laneSwing(size_t laneIndex) const noexcept {
        return laneIndex < kNumLanes ? laneSwingAmounts_[laneIndex] : 0.0f;
    }

    /// @brief Read per-lane length jitter (returns 0 if OOB).
    [[nodiscard]] int laneLengthJitter(size_t laneIndex) const noexcept {
        return laneIndex < kNumLanes ? laneLengthJitters_[laneIndex] : 0;
    }

    /// @brief Read per-lane speed-curve depth (returns 0 if OOB).
    [[nodiscard]] float laneSpeedCurveDepth(size_t laneIndex) const noexcept {
        return laneIndex < kNumLanes
            ? laneSpeedCurveDepths_[laneIndex].load(std::memory_order_relaxed)
            : 0.0f;
    }

    /// @brief Read per-lane speed-curve enabled flag (returns false if OOB).
    [[nodiscard]] bool laneSpeedCurveEnabled(size_t laneIndex) const noexcept {
        return laneIndex < kNumLanes
            && laneSpeedCurveEnabled_[laneIndex].load(std::memory_order_relaxed);
    }

    /// @brief Read per-lane speed-curve table (returns lane 0 if OOB).
    /// @note Call `consumePendingCurveTables()` first to pull staged updates.
    [[nodiscard]] const std::array<float, 256>&
    laneSpeedCurveTable(size_t laneIndex) const noexcept {
        return laneSpeedCurveTables_[laneIndex < kNumLanes ? laneIndex : 0];
    }

    // =========================================================================
    // Fill Mode (076-conditional-trigs, FR-020, FR-021)
    // =========================================================================

    /// @brief Set fill mode active state. Real-time safe, no side effects.
    void setFillActive(bool active) noexcept { fillActive_ = active; }

    /// @brief Get current fill mode state.
    [[nodiscard]] bool fillActive() const noexcept { return fillActive_; }

    /// @brief Set the accent velocity boost amount.
    /// @param amount Additive velocity boost for accented steps (0-127).
    void setAccentVelocity(int amount) noexcept {
        accentVelocity_ = std::clamp(amount, 0, 127);
    }

    /// @brief Set the slide portamento time.
    /// @param ms Portamento duration in milliseconds (0-500).
    void setSlideTime(float ms) noexcept {
        slideTimeMs_ = std::clamp(ms, 0.0f, 500.0f);
    }

    /// @brief Get the current accent velocity boost amount.
    /// @return Clamped accent velocity value in [0, 127].
    [[nodiscard]] int accentVelocity() const noexcept {
        return accentVelocity_;
    }

    /// @brief Get the current slide portamento time.
    /// @return Clamped slide time in milliseconds in [0, 500].
    [[nodiscard]] float slideTimeMs() const noexcept {
        return slideTimeMs_;
    }

    /// @brief Set ratchet swing as percentage 50-75%.
    /// Controls the long-short ratio within ratcheted sub-step pairs.
    /// 50% = equal spacing (default), 67% = triplet feel, 75% = dotted feel.
    /// Stored internally as 0.50-0.75 (divided by 100).
    void setRatchetSwing(float percent) noexcept {
        ratchetSwing_ = std::clamp(percent, 50.0f, 75.0f) / 100.0f;
    }

    /// @brief Set ratchet velocity decay as percentage 0-100%.
    /// Each subdivision's velocity is multiplied by (1 - decay)^subIndex,
    /// producing a bouncing-ball falloff. 0% = flat velocity, 100% = rapid decay.
    /// Stored internally as 0.0-1.0 (divided by 100).
    void setRatchetDecay(float percent) noexcept {
        ratchetDecay_ = std::clamp(percent, 0.0f, 100.0f) / 100.0f;
    }

    /// @brief Set strum time in milliseconds (0-100ms).
    /// Spreads chord note-on events in time for a guitar strum effect.
    /// 0ms = no strum (all notes simultaneous).
    void setStrumTime(float ms) noexcept {
        strumTimeMs_ = std::clamp(ms, 0.0f, 100.0f);
    }

    /// @brief Set strum direction: 0=Up, 1=Down, 2=Random, 3=Alternate.
    void setStrumDirection(int direction) noexcept {
        strumDirection_ = std::clamp(direction, 0, 3);
    }

    /// @brief Set per-lane swing as percentage 0-75%.
    /// laneIndex: 0=velocity, 1=gate, 2=pitch, 3=modifier,
    ///            4=ratchet, 5=condition, 6=chord, 7=inversion
    /// Skews each lane's advance timing independently: odd advances are
    /// delayed (even ones advanced) by the swing amount.
    void setLaneSwing(size_t laneIndex, float percent) noexcept {
        if (laneIndex >= kNumLanes) return;
        laneSwingAmounts_[laneIndex] = std::clamp(percent, 0.0f, 75.0f) / 100.0f;
    }

    /// @brief Set velocity curve type: 0=Linear, 1=Exponential, 2=Logarithmic, 3=S-Curve.
    void setVelocityCurveType(int type) noexcept {
        velocityCurveType_ = std::clamp(type, 0, 3);
    }

    /// @brief Set velocity curve amount as percentage 0-100%.
    /// 0% = no curve (linear passthrough), 100% = maximum curve shaping.
    void setVelocityCurveAmount(float percent) noexcept {
        velocityCurveAmount_ = std::clamp(percent, 0.0f, 100.0f) / 100.0f;
    }

    /// @brief Set global transpose in semitones (-24 to +24).
    /// When a non-chromatic scale is active, the transpose is quantized
    /// through the scale so the result always stays in key.
    void setTranspose(int semitones) noexcept {
        transpose_ = std::clamp(semitones, -24, 24);
    }

    /// @brief Set note range floor (MIDI 0-127).
    void setRangeLow(int midiNote) noexcept {
        rangeLow_ = std::clamp(midiNote, 0, 127);
    }

    /// @brief Set note range ceiling (MIDI 0-127).
    void setRangeHigh(int midiNote) noexcept {
        rangeHigh_ = std::clamp(midiNote, 0, 127);
    }

    /// @brief Set note range mode: 0=Wrap, 1=Clamp, 2=Skip.
    void setRangeMode(int mode) noexcept {
        rangeMode_ = std::clamp(mode, 0, 2);
    }

    /// @brief Set the global pin note (MIDI 0-127). All pinned steps
    /// emit this note instead of the arp-pattern note.
    void setPinNote(int midiNote) noexcept {
        pinNote_ = static_cast<uint8_t>(std::clamp(midiNote, 0, 127));
    }

    /// @brief Set per-step pin flag (0-31). When a step is pinned, the
    /// output note is overridden to the current pinNote_ (bypasses the
    /// arp pattern, octave, pitch offset, transpose, and range mapping).
    void setStepPinned(size_t stepIndex, bool pinned) noexcept {
        if (stepIndex < 32) pinFlags_[stepIndex] = pinned;
    }

    /// @brief Set per-lane length jitter in steps (0-4).
    /// Each time the given lane wraps, jitter randomly extends or shortens
    /// its effective length by up to +/- `steps` to create evolving patterns.
    /// laneIndex: 0=velocity, 1=gate, 2=pitch, 3=modifier,
    ///            4=ratchet, 5=condition, 6=chord, 7=inversion
    void setLaneLengthJitter(size_t laneIndex, int steps) noexcept {
        if (laneIndex >= kNumLanes) return;
        laneLengthJitters_[laneIndex] = std::clamp(steps, 0, 4);
    }

    // =========================================================================
    // Spice/Dice & Humanize (077-spice-dice-humanize)
    // =========================================================================

    /// Set Spice blend amount (0.0 = original, 1.0 = full overlay).
    void setSpice(float value) noexcept {
        spice_ = std::clamp(value, 0.0f, 1.0f);
    }

    /// Get current Spice blend amount.
    [[nodiscard]] float spice() const noexcept { return spice_; }

    /// Set Humanize amount (0.0 = quantized, 1.0 = max variation).
    void setHumanize(float value) noexcept {
        humanize_ = std::clamp(value, 0.0f, 1.0f);
    }

    /// Get current Humanize amount.
    [[nodiscard]] float humanize() const noexcept { return humanize_; }

    /// Generate new random overlay values for all four lanes (FR-005).
    /// Real-time safe: no allocation, no exceptions, no I/O.
    void triggerDice() noexcept {
        // Velocity: 32 unipolar floats in [0.0, 1.0]
        for (auto& v : velocityOverlay_) {
            v = spiceDiceRng_.nextUnipolar();
        }
        // Gate: 32 unipolar floats in [0.0, 1.0]
        for (auto& g : gateOverlay_) {
            g = spiceDiceRng_.nextUnipolar();
        }
        // Ratchet: 32 values in [1, 4]
        for (auto& r : ratchetOverlay_) {
            r = static_cast<uint8_t>(spiceDiceRng_.next() % 4 + 1);
        }
        // Condition: 32 values in [0, 17]
        for (auto& c : conditionOverlay_) {
            c = static_cast<uint8_t>(
                spiceDiceRng_.next() % static_cast<uint32_t>(TrigCondition::kCount));
        }
    }

    // =========================================================================
    // Scale Mode (084-arp-scale-mode)
    // =========================================================================

    /// Set the scale type for pitch lane interpretation.
    /// When non-Chromatic, pitch lane values are interpreted as scale degree offsets.
    /// When Chromatic, pitch lane values remain as semitone offsets (backward compatible).
    /// Does NOT reset arp state; safe to call unconditionally every block.
    void setScaleType(ScaleType type) noexcept {
        scaleHarmonizer_.setScale(type);
        // v1.7: Mirror into NoteSelector for Markov mode's scale-degree mapping.
        selector_.setMarkovScaleContext(type, scaleHarmonizer_.getKey());
    }

    /// Set the root note for the scale (0=C through 11=B).
    /// Does NOT reset arp state; safe to call unconditionally every block.
    void setRootNote(int rootNote) noexcept {
        scaleHarmonizer_.setKey(rootNote);
        // v1.7: Mirror into NoteSelector for Markov mode's scale-degree mapping.
        selector_.setMarkovScaleContext(scaleHarmonizer_.getScale(), rootNote);
    }

    /// v1.7: Set the 7x7 Markov transition matrix for ArpMode::Markov.
    /// Safe to call every block; no reset.
    void setMarkovMatrix(const std::array<float, kMarkovMatrixSize>& matrix) noexcept {
        selector_.setMarkovMatrix(matrix);
    }

    /// Enable/disable input note quantization.
    /// When enabled and scale is non-Chromatic, incoming noteOn pitches are
    /// snapped to the nearest scale note before entering the note pool.
    /// Does NOT reset arp state; safe to call unconditionally every block.
    void setScaleQuantizeInput(bool enabled) noexcept { scaleQuantizeInput_ = enabled; }

    // =========================================================================
    // Transport Sync
    // =========================================================================

    /// @brief Notify the arp that the DAW transport has looped (PPQ jumped
    /// backward). This triggers a clean restart: NoteOffs for sounding notes,
    /// lane/selector reset, and immediate step 0 firing — all within the
    /// same processBlock() call.
    ///
    /// Call from the processor when a backward PPQ jump is detected,
    /// BEFORE calling processBlock().
    void notifyTransportLoop() noexcept {
        transportLoopPending_ = true;
    }

    /// @brief Sync arp step clock to host musical position (PPQ).
    /// Call at block start when transport is playing and tempo sync is enabled.
    /// Aligns sampleCounter_ so step boundaries lock to the musical grid.
    /// Does NOT change which note the arp is playing (NoteSelector state is
    /// preserved) — only the timing within the current step is adjusted.
    ///
    /// @note When at a step boundary (stepFraction == 0), sets
    /// sampleCounter_ = currentStepDuration_ so the step fires immediately,
    /// consistent with the firstStepPending_ approach.
    void syncToMusicalPosition(double projectTimeMusic) noexcept {
        if (!tempoSync_ || currentStepDuration_ == 0) return;

        // For the arp, we only care about position within a single step
        // (not a multi-step pattern), because the arp doesn't have a fixed
        // pattern length — it advances through held notes via NoteSelector.
        const auto pos = calculateMusicalStepPosition(
            projectTimeMusic, noteValue_, noteModifier_, 1);

        // pos.stepFraction is the fractional position within one step [0, 1)
        const size_t newSampleCounter = static_cast<size_t>(
            pos.stepFraction * static_cast<double>(currentStepDuration_));

        // At a step boundary (fraction == 0), pre-load the counter to
        // trigger an immediate step fire, matching firstStepPending_ behavior.
        // During normal playback this is rare (block start rarely aligns
        // with a step boundary); at loop restart it ensures step 0 fires.
        sampleCounter_ = (newSampleCounter == 0)
            ? currentStepDuration_
            : newSampleCounter;
    }

    // =========================================================================
    // Processing (FR-019 through FR-024)
    // =========================================================================

    /// @brief Process one audio block, generating arp events.
    /// @param ctx Block context (tempo, sample rate, transport state)
    /// @param outputEvents Caller-owned span to receive events (capacity >= 64)
    /// @return Number of events written to outputEvents
    ///
    /// @pre outputEvents.size() >= kMaxEvents
    /// @post All events have sampleOffset in [0, ctx.blockSize-1]
    size_t processBlock(const BlockContext& ctx,
                               std::span<ArpEvent> outputEvents) noexcept;

private:
    // =========================================================================
    // Internal Types
    // =========================================================================

    /// @brief Tracks a pending NoteOff that spans across block boundaries.
    struct PendingNoteOff {
        uint8_t note{0};
        size_t samplesRemaining{0};
    };

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Calculate step duration in samples from current settings.
    /// Uses double precision to avoid float * double precision loss.
    /// Returns at minimum 1 sample.
    inline size_t calculateStepDuration(const BlockContext& ctx) const noexcept {
        size_t baseDuration = 0;
        if (tempoSync_) {
            // (f) Double-precision: cast getBeatsForNote() to double before multiply
            double beatsPerStep = static_cast<double>(
                getBeatsForNote(noteValue_, noteModifier_));
            double secondsPerBeat = 60.0 / ctx.tempoBPM;
            baseDuration = static_cast<size_t>(
                secondsPerBeat * beatsPerStep * ctx.sampleRate);
        } else {
            baseDuration = static_cast<size_t>(sampleRate_ / static_cast<double>(freeRateHz_));
        }

        // Apply swing
        size_t swungDuration = baseDuration;
        if (swing_ > 0.0f) {
            double swingVal = static_cast<double>(swing_);
            if (swingStepCounter_ % 2 == 0) {
                // Even step: lengthen
                swungDuration = static_cast<size_t>(
                    static_cast<double>(baseDuration) * (1.0 + swingVal));
            } else {
                // Odd step: shorten
                swungDuration = static_cast<size_t>(
                    static_cast<double>(baseDuration) * (1.0 - swingVal));
            }
        }

        // Clamp to minimum 1 sample
        return (swungDuration > 0) ? swungDuration : 1;
    }

    /// @brief Detect if a bar boundary falls within the current block.
    /// @return Offset within block (0 to blockSize-1) of the bar boundary,
    ///         or SIZE_MAX if no bar boundary falls within this block.
    /// Uses ctx.transportPositionSamples and ctx.samplesPerBar() (FR-023).
    inline size_t detectBarBoundary(const BlockContext& ctx) const noexcept {
        const size_t barSamples = ctx.samplesPerBar();
        if (barSamples == 0) {
            return SIZE_MAX;  // Prevent division by zero
        }

        const int64_t blockStart = ctx.transportPositionSamples;
        if (blockStart < 0) {
            return SIZE_MAX;  // Invalid transport position
        }

        const int64_t barSamplesI64 = static_cast<int64_t>(barSamples);
        const int64_t remainder = blockStart % barSamplesI64;

        int64_t barBoundarySample = 0;
        if (remainder == 0) {
            // Block starts exactly at a bar boundary
            barBoundarySample = 0;
        } else {
            // Next bar boundary is barSamples - remainder into the block
            barBoundarySample = barSamplesI64 - remainder;
        }

        // Check if bar boundary falls within [0, blockSize)
        if (barBoundarySample >= 0 &&
            barBoundarySample < static_cast<int64_t>(ctx.blockSize)) {
            return static_cast<size_t>(barBoundarySample);
        }

        return SIZE_MAX;  // No bar boundary in this block
    }

    /// @brief Calculate gate duration in samples from current step duration.
    /// Gate duration = stepDuration * gateLengthPercent / 100 * gateLaneValue,
    /// clamped to minimum 1 sample (FR-014: ensures NoteOff always fires).
    /// @param gateLaneValue Gate lane multiplier (default 1.0f for backward compat)
    inline size_t calculateGateDuration(float gateLaneValue = 1.0f) const noexcept {
        return std::max(size_t{1}, static_cast<size_t>(
            static_cast<double>(currentStepDuration_) *
            static_cast<double>(gateLengthPercent_) / 100.0 *
            static_cast<double>(gateLaneValue)));
    }

    /// @brief Compute per-sub-step durations and gate durations with ratchet swing.
    /// Groups sub-steps into consecutive pairs and applies swingRatio to each pair.
    /// Odd remainder (count 3) keeps baseDuration for the last sub-step.
    inline void computeSwungSubStepDurations(
        size_t stepDuration, uint8_t ratchetCount,
        float gateScale, float gateOffsetRatio) noexcept
    {
        const size_t baseDuration = stepDuration / static_cast<size_t>(ratchetCount);
        const size_t numPairs = static_cast<size_t>(ratchetCount) / 2;
        const bool hasRemainder = (ratchetCount % 2) != 0;

        for (size_t pair = 0; pair < numPairs; ++pair) {
            const size_t pairDuration = 2 * baseDuration;
            const size_t longDur = static_cast<size_t>(
                std::round(static_cast<double>(pairDuration) *
                           static_cast<double>(ratchetSwing_)));
            const size_t shortDur = pairDuration - longDur;

            const size_t evenIdx = pair * 2;
            const size_t oddIdx = evenIdx + 1;

            ratchetSubStepDurations_[evenIdx] = (longDur > 0) ? longDur : 1;
            ratchetSubStepDurations_[oddIdx] = (shortDur > 0) ? shortDur : 1;

            // Gate for each sub-step: subStepDuration * gate% / 100 * gateScale
            auto computeGate = [&](size_t dur) -> size_t {
                size_t gate = std::max(size_t{1}, static_cast<size_t>(
                    static_cast<double>(dur) *
                    static_cast<double>(gateLengthPercent_) / 100.0 *
                    static_cast<double>(gateScale)));
                // Apply humanize gate offset
                int32_t humanizedGate = static_cast<int32_t>(gate)
                    + static_cast<int32_t>(static_cast<float>(gate) * gateOffsetRatio);
                return static_cast<size_t>(std::max(int32_t{1}, humanizedGate));
            };
            ratchetGateDurations_[evenIdx] = computeGate(longDur > 0 ? longDur : 1);
            ratchetGateDurations_[oddIdx] = computeGate(shortDur > 0 ? shortDur : 1);
        }

        if (hasRemainder) {
            const size_t lastIdx = static_cast<size_t>(ratchetCount) - 1;
            ratchetSubStepDurations_[lastIdx] = (baseDuration > 0) ? baseDuration : 1;
            size_t gate = std::max(size_t{1}, static_cast<size_t>(
                static_cast<double>(baseDuration) *
                static_cast<double>(gateLengthPercent_) / 100.0 *
                static_cast<double>(gateScale)));
            int32_t humanizedGate = static_cast<int32_t>(gate)
                + static_cast<int32_t>(static_cast<float>(gate) * gateOffsetRatio);
            ratchetGateDurations_[lastIdx] = static_cast<size_t>(
                std::max(int32_t{1}, humanizedGate));
        }
    }

    /// @brief Decrement all pending NoteOff samplesRemaining by given amount.
    inline void decrementPendingNoteOffs(size_t samples) noexcept {
        for (size_t i = 0; i < pendingNoteOffCount_; ++i) {
            if (pendingNoteOffs_[i].samplesRemaining >= samples) {
                pendingNoteOffs_[i].samplesRemaining -= samples;
            } else {
                pendingNoteOffs_[i].samplesRemaining = 0;
            }
        }
    }

    /// @brief Emit all pending NoteOffs whose samplesRemaining == 0.
    /// Removes them from the array by compacting.
    inline void emitDuePendingNoteOffs(int32_t sampleOffset,
                                        std::span<ArpEvent> outputEvents,
                                        size_t& eventCount,
                                        size_t maxEvents) noexcept {
        size_t i = 0;
        while (i < pendingNoteOffCount_ && eventCount < maxEvents) {
            if (pendingNoteOffs_[i].samplesRemaining == 0) {
                // Emit NoteOff event
                outputEvents[eventCount++] = ArpEvent{
                    ArpEvent::Type::NoteOff,
                    pendingNoteOffs_[i].note,
                    0,
                    sampleOffset};

                // Remove from currentArpNotes_ tracking
                removeFromCurrentArpNotes(pendingNoteOffs_[i].note);

                // Compact: move last element to this slot
                pendingNoteOffs_[i] = pendingNoteOffs_[pendingNoteOffCount_ - 1];
                --pendingNoteOffCount_;
                // Don't increment i -- re-check the swapped element
            } else {
                ++i;
            }
        }
    }

    /// @brief Add a pending NoteOff to the array.
    /// If at capacity, emit the oldest one at sampleOffset 0 (overflow handling).
    inline void addPendingNoteOff(uint8_t note, size_t samplesRemaining,
                                   std::span<ArpEvent> outputEvents,
                                   size_t& eventCount,
                                   size_t maxEvents) noexcept {
        if (pendingNoteOffCount_ >= kMaxPendingNoteOffs) {
            // Overflow: emit oldest (first entry) at sampleOffset 0
            if (eventCount < maxEvents) {
                outputEvents[eventCount++] = ArpEvent{
                    ArpEvent::Type::NoteOff,
                    pendingNoteOffs_[0].note,
                    0,
                    0};
                removeFromCurrentArpNotes(pendingNoteOffs_[0].note);
            }
            // Compact: shift everything down
            for (size_t i = 1; i < pendingNoteOffCount_; ++i) {
                pendingNoteOffs_[i - 1] = pendingNoteOffs_[i];
            }
            --pendingNoteOffCount_;
        }

        pendingNoteOffs_[pendingNoteOffCount_] = PendingNoteOff{note, samplesRemaining};
        ++pendingNoteOffCount_;
    }

    /// @brief Apply a curve transform to a normalized 0-1 velocity value.
    /// Returns the curved value (also 0-1). (v1.5 Velocity Curve)
    [[nodiscard]] inline float applyVelocityCurve(float x) const noexcept {
        x = std::clamp(x, 0.0f, 1.0f);
        switch (velocityCurveType_) {
            case 1: // Exponential — slow rise, fast end (x^2)
                return x * x;
            case 2: // Logarithmic — fast rise, slow end (sqrt)
                return std::sqrt(x);
            case 3: // S-Curve — smooth ease in/out (smoothstep)
                return x * x * (3.0f - 2.0f * x);
            case 0:
            default:
                return x; // Linear (no change)
        }
    }

    /// @brief Precompute per-note strum offsets into strumOffsets_ for a chord.
    /// Called ONCE at the start of each chord emission. Direction is picked
    /// once per chord (important for Random/Alternate modes). (v1.5 Strum Mode)
    inline void prepareStrumOffsets(size_t noteCount) noexcept {
        // Clear first
        strumOffsets_.fill(0);

        if (strumTimeMs_ <= 0.0f || noteCount <= 1) return;

        const float totalSamples = strumTimeMs_ * 0.001f
            * static_cast<float>(sampleRate_);
        const float perNoteSamples = totalSamples
            / static_cast<float>(noteCount - 1);

        // Pick a single direction for this entire chord
        int direction = strumDirection_;
        if (direction == 3) {
            // Alternate: flip between Up and Down on each chord
            direction = (strumAlternateCounter_++ & 1u) == 0u ? 0 : 1;
        } else if (direction == 2) {
            // Random: xorshift to pick Up or Down
            uint32_t& s = strumRandomState_;
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            direction = (s & 1u) == 0u ? 0 : 1;
        }

        for (size_t i = 0; i < noteCount && i < 32; ++i) {
            float index = (direction == 0)
                ? static_cast<float>(i)                   // Up
                : static_cast<float>(noteCount - 1 - i);  // Down
            strumOffsets_[i] = static_cast<int32_t>(index * perNoteSamples);
        }
    }

    /// @brief Get precomputed strum offset for a note index (after prepareStrumOffsets).
    [[nodiscard]] inline int32_t strumOffsetFor(size_t noteIndex) const noexcept {
        return (noteIndex < 32) ? strumOffsets_[noteIndex] : 0;
    }

    /// @brief Fire a ratchet sub-step: emit noteOn(s), schedule noteOff, advance state.
    /// Called from processBlock() when NextEvent::SubStep fires or when a NoteOff
    /// coincides with a sub-step boundary. (074-ratcheting, FR-015)
    void fireSubStep([[maybe_unused]] const BlockContext& ctx,
                             int32_t sampleOffset,
                             std::span<ArpEvent> outputEvents,
                             size_t& eventCount,
                             size_t maxEvents) noexcept;

    // =========================================================================
    // Condition Evaluation (076-conditional-trigs, FR-013)
    // =========================================================================

    /// @brief Evaluate a TrigCondition for the current step.
    /// @param condition The condition to evaluate (TrigCondition enum as uint8_t)
    /// @return true if the step should fire, false if it should be treated as rest
    /// Consumes conditionRng_ only for probability conditions (Prob10-Prob90).
    /// Uses loopCount_ for A:B ratio and First conditions.
    /// Uses fillActive_ for Fill/NotFill conditions.
    /// Values >= kCount are treated as Always (defensive fallback).
    inline bool evaluateCondition(uint8_t condition) noexcept {
        const auto cond = static_cast<TrigCondition>(condition);
        switch (cond) {
            case TrigCondition::Always:
                return true;
            case TrigCondition::Prob10:
                return conditionRng_.nextUnipolar() < 0.10f;
            case TrigCondition::Prob25:
                return conditionRng_.nextUnipolar() < 0.25f;
            case TrigCondition::Prob50:
                return conditionRng_.nextUnipolar() < 0.50f;
            case TrigCondition::Prob75:
                return conditionRng_.nextUnipolar() < 0.75f;
            case TrigCondition::Prob90:
                return conditionRng_.nextUnipolar() < 0.90f;
            case TrigCondition::Ratio_1_2:
                return loopCount_ % 2 == 0;
            case TrigCondition::Ratio_2_2:
                return loopCount_ % 2 == 1;
            case TrigCondition::Ratio_1_3:
                return loopCount_ % 3 == 0;
            case TrigCondition::Ratio_2_3:
                return loopCount_ % 3 == 1;
            case TrigCondition::Ratio_3_3:
                return loopCount_ % 3 == 2;
            case TrigCondition::Ratio_1_4:
                return loopCount_ % 4 == 0;
            case TrigCondition::Ratio_2_4:
                return loopCount_ % 4 == 1;
            case TrigCondition::Ratio_3_4:
                return loopCount_ % 4 == 2;
            case TrigCondition::Ratio_4_4:
                return loopCount_ % 4 == 3;
            case TrigCondition::First:
                return loopCount_ == 0;
            case TrigCondition::Fill:
                return fillActive_;
            case TrigCondition::NotFill:
                return !fillActive_;
            default:
                return true;  // Out-of-range: treat as Always (defensive)
        }
    }

    /// @brief Fire a step: advance NoteSelector, emit NoteOn, schedule NoteOff.
    void fireStep(const BlockContext& ctx,
                          int32_t sampleOffset,
                          std::span<ArpEvent> outputEvents,
                          size_t& eventCount,
                          size_t maxEvents,
                          [[maybe_unused]] size_t samplesProcessed,
                          [[maybe_unused]] size_t blockSize) noexcept;

    /// @brief Emit NoteOffs at sampleOffset 0 for every sounding arp note and
    /// every still-pending NoteOff, then clear both trackers.
    /// Shared by the disabled path, the empty-held-buffer path and the
    /// unconditional panic discharge -- they had three copies of this loop.
    inline void emitPanicNoteOffs(std::span<ArpEvent> outputEvents,
                                  size_t& eventCount,
                                  size_t maxEvents) noexcept {
        for (size_t i = 0; i < currentArpNoteCount_ && eventCount < maxEvents; ++i) {
            outputEvents[eventCount++] = ArpEvent{
                .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[i],
                .velocity = 0, .sampleOffset = 0};
        }
        // Pending NoteOffs track notes that are also in currentArpNotes_, so
        // skip any we just emitted (FR-027, 082-presets-polish).
        for (size_t i = 0; i < pendingNoteOffCount_ && eventCount < maxEvents; ++i) {
            bool alreadyEmitted = false;
            for (size_t j = 0; j < currentArpNoteCount_; ++j) {
                if (pendingNoteOffs_[i].note == currentArpNotes_[j]) {
                    alreadyEmitted = true;
                    break;
                }
            }
            if (!alreadyEmitted) {
                outputEvents[eventCount++] = ArpEvent{
                    .type = ArpEvent::Type::NoteOff, .note = pendingNoteOffs_[i].note,
                    .velocity = 0, .sampleOffset = 0};
            }
        }
        currentArpNoteCount_ = 0;
        pendingNoteOffCount_ = 0;
        needsDisableNoteOff_ = false;
        panicRequested_ = false;
    }

    /// @brief Clamp an emission offset into the host's [0, blockSize) window.
    ///
    /// `humanizedSampleOffset` is already clamped, but the strum offset (up to
    /// strumTimeMs = 100 ms, i.e. 4800 samples at 48 kHz) is added afterwards
    /// and can push a strummed chord note thousands of samples past the end of a
    /// 64-sample host block. Neither the processor nor MidiNoteDelay re-clamps,
    /// so such an event reaches the host out of range and is dropped -- while
    /// its gate NoteOff, scheduled in range, still fires.
    [[nodiscard]] static inline int32_t clampOffsetToBlock(
        int32_t offset, size_t blockSize) noexcept {
        const int32_t last =
            (blockSize > 0) ? static_cast<int32_t>(blockSize) - 1 : 0;
        return std::clamp(offset, int32_t{0}, last);
    }

    /// @brief Drop the first `releasedCount` tracked notes, keeping the rest.
    ///
    /// The chord/ratchet replace loops stop at the output-span cap, so they can
    /// release only a prefix of `currentArpNotes_`. Zeroing the count regardless
    /// would erase the un-released remainder from tracking: after a Tie/Slide
    /// step (which schedules no pending NoteOff) those notes would have no
    /// output NoteOff, no pending NoteOff and no tracking entry, leaving them
    /// unreleasable even by a panic flush.
    inline void retainUnreleasedArpNotes(size_t releasedCount) noexcept {
        if (releasedCount >= currentArpNoteCount_) {
            currentArpNoteCount_ = 0;
            return;
        }
        const size_t remaining = currentArpNoteCount_ - releasedCount;
        for (size_t i = 0; i < remaining; ++i) {
            currentArpNotes_[i] = currentArpNotes_[releasedCount + i];
        }
        currentArpNoteCount_ = remaining;
    }

    /// @brief Remove a note from the currentArpNotes_ tracking array.
    inline void removeFromCurrentArpNotes(uint8_t note) noexcept {
        for (size_t i = 0; i < currentArpNoteCount_; ++i) {
            if (currentArpNotes_[i] == note) {
                // Compact: move last to this slot
                currentArpNotes_[i] = currentArpNotes_[currentArpNoteCount_ - 1];
                --currentArpNoteCount_;
                return;
            }
        }
    }

    /// @brief Cancel all pending noteOffs for currently sounding notes.
    /// Used by Tie evaluation to override gate-based noteOff scheduling (FR-012).
    inline void cancelPendingNoteOffsForCurrentNotes() noexcept {
        for (size_t n = 0; n < currentArpNoteCount_; ++n) {
            uint8_t note = currentArpNotes_[n];
            // Remove all pending noteOffs matching this note
            size_t i = 0;
            while (i < pendingNoteOffCount_) {
                if (pendingNoteOffs_[i].note == note) {
                    pendingNoteOffs_[i] = pendingNoteOffs_[pendingNoteOffCount_ - 1];
                    --pendingNoteOffCount_;
                } else {
                    ++i;
                }
            }
        }
    }

    /// @brief Process pending NoteOffs for the entire block when no new steps fire.
    /// Used when heldNotes_ is empty but pending NoteOffs remain.
    inline void processPendingNoteOffsForBlock(
        size_t blockSize,
        std::span<ArpEvent> outputEvents,
        size_t& eventCount,
        size_t maxEvents) noexcept {
        // Process pending NoteOffs that fire within this block
        for (size_t i = 0; i < pendingNoteOffCount_;) {
            if (pendingNoteOffs_[i].samplesRemaining < blockSize &&
                eventCount < maxEvents) {
                int32_t offset = static_cast<int32_t>(
                    pendingNoteOffs_[i].samplesRemaining);
                outputEvents[eventCount++] = ArpEvent{
                    ArpEvent::Type::NoteOff,
                    pendingNoteOffs_[i].note,
                    0,
                    offset};
                removeFromCurrentArpNotes(pendingNoteOffs_[i].note);
                // Compact
                pendingNoteOffs_[i] = pendingNoteOffs_[pendingNoteOffCount_ - 1];
                --pendingNoteOffCount_;
            } else {
                pendingNoteOffs_[i].samplesRemaining -= blockSize;
                ++i;
            }
        }
    }

    // =========================================================================
    // Euclidean Pattern Helper (075-euclidean-timing, FR-008)
    // =========================================================================

    /// @brief Regenerate the Euclidean pattern bitmask from current parameters.
    /// Called by setters and constructor to keep euclideanPattern_ in sync.
    inline void regenerateEuclideanPattern() noexcept {
        euclideanPattern_ = EuclideanPattern::generate(
            euclideanHits_, euclideanSteps_, euclideanRotation_);
    }

    // =========================================================================
    // Lane Reset (072-independent-lanes)
    // =========================================================================

    /// @brief Reset all lane positions to step 0.
    /// Called from reset(), retrigger, and transport restart points.
    void resetLanes() noexcept {
        velocityLane_.reset();
        gateLane_.reset();
        pitchLane_.reset();
        modifierLane_.reset();   // 073-per-step-mods: reset modifier lane position
        tieActive_ = false;       // 073-per-step-mods: reset tie chain state
        ratchetLane_.reset();              // 074-ratcheting: reset ratchet lane position
        ratchetSubStepsRemaining_ = 0;     // 074-ratcheting: clear sub-step state
        ratchetSubStepCounter_ = 0;
        ratchetSubStepIndex_ = 0;
        euclideanPosition_ = 0;            // 075-euclidean-timing: reset Euclidean position (FR-013)
        conditionLane_.reset();              // 076-conditional-trigs: reset condition lane position
        chordLane_.reset();                  // arp-chord-lane: reset chord lane position
        inversionLane_.reset();              // arp-chord-lane: reset inversion lane position
        midiDelayLane_.reset();              // MIDI delay lane: reset position
        seqNoteLane_.reset();                // Spec 142: Sequencer Note lane (lane 10) playhead reset
        loopCount_ = 0;                      // 076-conditional-trigs: reset loop counter
        // fillActive_ intentionally NOT reset (FR-022: performance control)
        // conditionRng_ intentionally NOT reset (FR-035: continuous randomness)
        // 077-spice-dice-humanize: overlays/Spice/Humanize intentionally NOT reset (FR-025-029)
        // velocityOverlay_, gateOverlay_, ratchetOverlay_, conditionOverlay_ preserved
        // spice_, humanize_ preserved (user-controlled parameters)
        // spiceDiceRng_, humanizeRng_ preserved (continuous randomness, like conditionRng_)
        // Reset lane speed accumulators
        laneAccumulators_.fill(0.0f);
        // v1.5: Reset per-lane swing phase counters (net-zero drift after reset)
        laneSwingCounters_.fill(0);
        // v1.5 Part 2: Reset length jitter state
        lanePendingSkips_.fill(0);
        laneLastSteps_.fill(0);
    }

    // =========================================================================
    // Composed Components (Layer 1)
    // =========================================================================

    HeldNoteBuffer heldNotes_;
    NoteSelector selector_{42};  ///< Seed 42 for deterministic random

    // =========================================================================
    // Lane Containers (072-independent-lanes, Layer 1)
    // =========================================================================

    ArpLane<float> velocityLane_;   ///< Velocity multiplier per step (default: length=1, step[0]=1.0f)
    ArpLane<float> gateLane_;       ///< Gate duration multiplier per step (default: length=1, step[0]=1.0f)
    ArpLane<int8_t> pitchLane_;    ///< Semitone offset per step (default: length=1, step[0]=0)
    ArpLane<uint8_t> modifierLane_; ///< Bitmask per step (default: length=1, step[0]=kStepActive)
    ArpLane<uint8_t> ratchetLane_;  ///< Per-step ratchet count 1-4 (default: length=1, step[0]=1)

    // =========================================================================
    // Chord Lane Containers (arp-chord-lane)
    // =========================================================================

    ArpLane<uint8_t> chordLane_;     ///< Per-step ChordType (default: length=1, step[0]=0=None)
    ArpLane<uint8_t> inversionLane_; ///< Per-step InversionType (default: length=1, step[0]=0=Root)
    ArpLane<uint8_t> midiDelayLane_; ///< MIDI delay step tracking (lane index 8)
    /// Sequencer Note lane (spec 142 — lane index 9). Stores 32 MIDI pitches.
    /// Conditionally inert in Live mode: fireStep skips advance + emission.
    ArpLane<uint8_t> seqNoteLane_;
    /// Per-step rest flags for the Sequencer Note lane (FR-019). Atomic so
    /// the controller's UI thread can update; the audio thread reads on each
    /// step boundary. Default = 1 (rest) so a fresh pattern is silent until
    /// the user populates it.
    std::array<std::atomic<uint8_t>, 32> seqRestFlags_{};
    /// Note source mode (spec 142). Default Live — Ruinae never overrides.
    SourceMode sourceMode_{SourceMode::Live};
    VoicingMode voicingMode_{VoicingMode::Close}; ///< Global voicing mode

    // =========================================================================
    // Per-Lane Speed Multipliers
    // =========================================================================
    // Lane index: 0=velocity, 1=gate, 2=pitch, 3=modifier, 4=ratchet,
    //             5=condition, 6=chord, 7=inversion, 8=midiDelay, 9=seqNote (spec 142)
    std::array<float, kNumLanes> laneSpeedMultipliers_{
        {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<float, kNumLanes> laneAccumulators_{
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};

    // =========================================================================
    // Per-Lane Speed Curves
    // =========================================================================
    // Baked 256-entry lookup tables modulate the speed multiplier over one loop
    // cycle. curveDepths_ controls the offset range; curveEnabled_ gates the
    // effect. Tables are sent from the controller via IMessage.
    //
    // Thread safety: tables are handed from the message thread (via notify())
    // to the audio thread through a per-lane TRIPLE buffer, then copied into the
    // active table in consumePendingCurveTables().
    //
    // Three slots, not one and not two. One was a plain data race: a 1 KB copy
    // is nowhere near atomic, so a second publish landing mid-copy rewrote the
    // bytes the audio thread was reading (ThreadSanitizer flags it directly).
    // Two is still not enough -- while the reader holds the published slot, two
    // further publishes wrap the writer straight back onto it. With three, the
    // writer and reader each own a slot outright and `ready` holds the third, so
    // the two never alias. Ownership rotates through the exchange below; the
    // low bits carry the slot index and the top bit marks unread data.
    static constexpr uint8_t kCurveSlotMask = 0x03;
    static constexpr uint8_t kCurveDirtyBit = 0x80;
    std::array<std::array<float, 256>, kNumLanes> laneSpeedCurveTables_{};
    std::array<std::array<std::array<float, 256>, 3>, kNumLanes>
        laneSpeedCurveTablesStaging_{};
    /// Published slot + dirty bit. Starts at slot 0, clean.
    std::array<std::atomic<uint8_t>, kNumLanes> laneSpeedCurveReady_{};
    /// Writer-owned slot (message thread only). Starts at 1.
    std::array<uint8_t, kNumLanes> laneSpeedCurveWriteSlot_{};
    /// Reader-owned slot (audio thread only). Starts at 2.
    std::array<uint8_t, kNumLanes> laneSpeedCurveReadSlot_{};
    // Written from the host message thread (Gradus routes depth through
    // Processor::notify), read on the audio thread every block -- atomic for
    // the same reason as laneSpeedCurveEnabled_ below.
    std::array<std::atomic<float>, kNumLanes> laneSpeedCurveDepths_{};
    std::array<std::atomic<bool>, kNumLanes> laneSpeedCurveEnabled_{};

    // =========================================================================
    // Modifier Configuration (073-per-step-mods)
    // =========================================================================

    int accentVelocity_{30};        ///< Additive velocity boost for accented steps (0-127)
    float slideTimeMs_{60.0f};      ///< Portamento duration (0-500ms). Stored for API symmetry.
    bool tieActive_{false};         ///< True when in a tie chain. Cleared by resetLanes().

    // =========================================================================
    // Ratchet Sub-Step State (074-ratcheting, FR-011)
    // =========================================================================

    uint8_t ratchetSubStepsRemaining_{0};      ///< Sub-steps left to fire (0 = inactive)
    std::array<size_t, 4> ratchetSubStepDurations_{}; ///< Per-sub-step durations in samples
    std::array<size_t, 4> ratchetGateDurations_{};    ///< Per-sub-step gate durations
    uint8_t ratchetSubStepIndex_{0};           ///< Which sub-step we're counting through
    uint8_t ratchetTotalSubSteps_{0};          ///< Total ratchet count for current step
    size_t ratchetSubStepCounter_{0};          ///< Sample counter within current sub-step
    uint8_t ratchetNote_{0};                   ///< MIDI note for retriggers (single note)
    uint8_t ratchetVelocity_{0};               ///< Non-accented velocity for retriggers
    bool ratchetIsLastSubStep_{false};         ///< True when firing last sub-step (for look-ahead)
    std::array<uint8_t, 32> ratchetNotes_{};   ///< Chord mode note numbers
    std::array<uint8_t, 32> ratchetVelocities_{}; ///< Chord mode velocities
    size_t ratchetNoteCount_{0};               ///< Chord mode note count
    float ratchetSwing_{0.50f};                ///< Ratchet swing ratio 0.50-0.75
    float ratchetDecay_{0.0f};                 ///< Ratchet velocity decay 0.0-1.0 (v1.5)

    // v1.5: Strum Mode state
    float strumTimeMs_{0.0f};                  ///< Strum time in milliseconds
    int strumDirection_{0};                    ///< 0=Up, 1=Down, 2=Random, 3=Alternate
    uint32_t strumAlternateCounter_{0};        ///< For Alternate direction
    uint32_t strumRandomState_{0xABCDEF01u};   ///< Xorshift state for Random
    std::array<int32_t, 32> strumOffsets_{};   ///< Per-chord precomputed offsets

    // v1.5: Per-lane swing
    std::array<float, kNumLanes> laneSwingAmounts_{
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    std::array<uint8_t, kNumLanes> laneSwingCounters_{
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

    // v1.5 Part 2
    int velocityCurveType_{0};        ///< 0=Linear, 1=Exp, 2=Log, 3=S-Curve
    float velocityCurveAmount_{0.0f}; ///< 0.0-1.0 blend amount
    int transpose_{0};                ///< -24 to +24 semitones
    std::array<int, kNumLanes> laneLengthJitters_{};    ///< Per-lane jitter amount (0-4 steps)
    std::array<int8_t, kNumLanes> lanePendingSkips_{};  ///< Positive = skip next N advances (lengthens)
    std::array<uint8_t, kNumLanes> laneLastSteps_{};    ///< Previous step position for wrap detection
    uint32_t lengthJitterRng_{0xFEEDBEEFu};     ///< Xorshift state for jitter re-rolls

    // v1.5 Part 3: Note Range Mapping
    int rangeLow_{0};    ///< MIDI floor (0-127)
    int rangeHigh_{127}; ///< MIDI ceiling (0-127)
    int rangeMode_{1};   ///< 0=Wrap, 1=Clamp, 2=Skip

    // v1.5 Part 3: Step Pinning
    uint8_t pinNote_{60};               ///< MIDI note for pinned steps
    std::array<bool, 32> pinFlags_{};   ///< Per-step pin state (indexed by pitch lane step)

    // =========================================================================
    // Euclidean Timing State (075-euclidean-timing, FR-001)
    // =========================================================================

    bool euclideanEnabled_{false};        ///< Whether Euclidean gating is active
    int euclideanHits_{4};                ///< Number of pulses (k), range [0, 32]
    int euclideanSteps_{8};               ///< Number of steps (n), range [2, 32]
    int euclideanRotation_{0};            ///< Rotation offset, range [0, 31]
    size_t euclideanPosition_{0};         ///< Current position in Euclidean pattern
    uint32_t euclideanPattern_{0};        ///< Pre-computed bitmask from generate()

    // =========================================================================
    // Condition State (076-conditional-trigs)
    // =========================================================================

    ArpLane<uint8_t> conditionLane_;     ///< Per-step condition (TrigCondition as uint8_t)
    size_t loopCount_{0};                ///< Condition lane cycle counter
    bool fillActive_{false};             ///< Fill mode performance toggle
    Xorshift32 conditionRng_{7919};      ///< Dedicated PRNG for probability (prime seed)

    // =========================================================================
    // Spice/Dice State (077-spice-dice-humanize)
    // =========================================================================

    /// Variation overlay arrays generated by triggerDice().
    /// Indexed by each lane's own step position (polymetric-aware).
    std::array<float, 32> velocityOverlay_{};    ///< [0.0, 1.0] velocity scaling
    std::array<float, 32> gateOverlay_{};        ///< [0.0, 1.0] gate scaling
    std::array<uint8_t, 32> ratchetOverlay_{};   ///< [1, 4] ratchet count
    std::array<uint8_t, 32> conditionOverlay_{}; ///< [0, 17] TrigCondition value

    float spice_{0.0f};                          ///< Blend amount [0, 1]
    float humanize_{0.0f};                       ///< Humanize amount [0, 1]
    Xorshift32 spiceDiceRng_{31337};             ///< PRNG for overlay generation
    Xorshift32 humanizeRng_{48271};              ///< PRNG for per-step offsets

    // =========================================================================
    // Configuration State
    // =========================================================================

    bool enabled_ = false;
    ArpMode arpMode_ = ArpMode::Up;  ///< Cached arp mode for chord lane skip check
    LatchMode latchMode_ = LatchMode::Off;
    ArpRetriggerMode retriggerMode_ = ArpRetriggerMode::Off;
    bool tempoSync_ = true;
    NoteValue noteValue_ = NoteValue::Eighth;
    NoteModifier noteModifier_ = NoteModifier::None;
    float freeRateHz_ = 4.0f;
    float gateLengthPercent_ = 80.0f;
    float swing_ = 0.0f;  ///< Stored as 0.0-0.75 (user-facing 0-75% divided by 100)

    // =========================================================================
    // Timing State
    // =========================================================================

    double sampleRate_ = 44100.0;
    size_t sampleCounter_ = 0;
    size_t currentStepDuration_ = 0;
    size_t swingStepCounter_ = 0;
    bool wasPlaying_ = false;
    bool firstStepPending_ = true;
    bool transportLoopPending_ = false;

    // =========================================================================
    // Latch State
    // =========================================================================

    size_t physicalKeysHeld_ = 0;
    bool latchActive_ = false;

    // =========================================================================
    // NoteOff Tracking (FR-025, FR-026)
    // =========================================================================

    std::array<uint8_t, 32> currentArpNotes_{};
    size_t currentArpNoteCount_ = 0;
    // Sized from the capacity constant, not a literal: addPendingNoteOff bounds
    // itself on kMaxPendingNoteOffs, so a hardcoded size here means raising the
    // constant writes past the end of this array.
    std::array<PendingNoteOff, kMaxPendingNoteOffs> pendingNoteOffs_{};
    size_t pendingNoteOffCount_ = 0;

    // =========================================================================
    // Disable Transition (FR-008)
    // =========================================================================

    bool needsDisableNoteOff_ = false;
    /// Set only by requestPanicNoteOff(); discharged unconditionally at the top
    /// of processBlock (see that method's note).
    bool panicRequested_ = false;

    // =========================================================================
    // Scale Mode (084-arp-scale-mode)
    // =========================================================================

    ScaleHarmonizer scaleHarmonizer_;       ///< Scale calculator for pitch lane and input quantization
    bool scaleQuantizeInput_ = false;       ///< Whether to snap incoming notes to scale
};

} // namespace Krate::DSP
