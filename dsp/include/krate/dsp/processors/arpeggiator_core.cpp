// arpeggiator_core.cpp
// Out-of-line definitions for the three largest ArpeggiatorCore methods
// (processBlock, fireSubStep, fireStep), moved out of the header (D4) to shrink a
// 2900-line header-only file and cut its compile cost. Layer-2, .cpp precedent:
// core/spectral_simd.cpp. These functions are far too large to inline anyway, so
// moving them out has no runtime cost.
#include <krate/dsp/processors/arpeggiator_core.h>

namespace Krate::DSP {

size_t ArpeggiatorCore::processBlock(const BlockContext& ctx,
                               std::span<ArpEvent> outputEvents) noexcept {
        // (a) Zero blockSize guard (FR-032) -- no state change
        if (ctx.blockSize == 0) {
            return 0;
        }

        // Cap event emission to output span size
        const size_t maxEvents = (outputEvents.size() < kMaxEvents)
                                     ? outputEvents.size()
                                     : kMaxEvents;

        size_t eventCount = 0;

        // (b) Explicit panic discharge. An explicitly requested panic must
        // always fire, whatever the enabled state, transport, source mode, or
        // whether keys are still held -- the needsDisableNoteOff_ paths below
        // only run when the arp is disabled or the held buffer just emptied, so
        // a panic raised mid-chord (source toggle, host deactivation) would
        // otherwise sit unconsumed and strand the sounding notes.
        if (panicRequested_) {
            emitPanicNoteOffs(outputEvents, eventCount, maxEvents);
        }

        // (c) Disabled check (FR-008)
        if (!enabled_) {
            if (needsDisableNoteOff_) {
                emitPanicNoteOffs(outputEvents, eventCount, maxEvents);
            }
            return eventCount;
        }

        // (d) Transport not playing check (FR-031)
        if (!ctx.isPlaying) {
            if (wasPlaying_) {
                // Transport just stopped -- emit NoteOffs for all currently
                // sounding arp notes at sampleOffset 0.
                // currentArpNotes_ tracks all notes with active gate;
                // pendingNoteOffs_ tracks the same notes' scheduled NoteOffs.
                // We emit from currentArpNotes_ only to avoid duplicates.
                for (size_t i = 0; i < currentArpNoteCount_ && eventCount < maxEvents; ++i) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[i], .velocity = 0, .sampleOffset = 0};
                }
                currentArpNoteCount_ = 0;
                pendingNoteOffCount_ = 0;
                ratchetSubStepsRemaining_ = 0;  // 074-ratcheting (FR-027)
                ratchetSubStepCounter_ = 0;
                ratchetSubStepIndex_ = 0;
            }
            wasPlaying_ = false;
            return eventCount;
        }

        // Handle transport loop (DAW cycle): emit NoteOffs for sounding
        // notes, reset lanes/selector, and prepare for immediate step 0.
        if (transportLoopPending_) {
            transportLoopPending_ = false;
            // Emit NoteOffs for all currently sounding arp notes
            for (size_t i = 0; i < currentArpNoteCount_ && eventCount < maxEvents; ++i) {
                outputEvents[eventCount++] = ArpEvent{
                    .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[i], .velocity = 0, .sampleOffset = 0};
            }
            currentArpNoteCount_ = 0;
            pendingNoteOffCount_ = 0;
            ratchetSubStepsRemaining_ = 0;
            ratchetSubStepCounter_ = 0;
            ratchetSubStepIndex_ = 0;
            tieActive_ = false;
            // Reset pattern state for clean restart
            firstStepPending_ = true;
            sampleCounter_ = 0;
            selector_.reset();
            swingStepCounter_ = 0;
            resetLanes();
        }

        // Handle transport restart (FR-023, FR-025): reset step counters
        // and lane positions so the arp restarts cleanly from step 1.
        if (!wasPlaying_) {
            wasPlaying_ = true;
            firstStepPending_ = true;
            sampleCounter_ = 0;
            selector_.reset();
            swingStepCounter_ = 0;
            resetLanes();
        }

        // (g) firstStepPending_: compute initial step duration and trigger
        // immediate first step by pre-loading the sample counter so the main
        // loop fires a step at the current sample offset (FR-023, FR-025).
        if (firstStepPending_) {
            currentStepDuration_ = calculateStepDuration(ctx);
            firstStepPending_ = false;
            sampleCounter_ = currentStepDuration_;
        }

        // Empty buffer with latch Off: emit NoteOff for current arp notes,
        // flush all pending NoteOffs, then return (FR-007, FR-024).
        // Spec 142: in Sequencer mode the pattern (lane 9) is the source,
        // so an empty held buffer must NOT short-circuit playback. Held
        // notes only supply transposition root + base velocity in Seq mode.
        if (heldNotes_.empty() && sourceMode_ != SourceMode::Sequencer) {
            // If buffer just became empty (latch Off, all keys released),
            // emit NoteOff for currently sounding arp note(s) and flush
            // ALL pending NoteOffs at sampleOffset 0 to prevent stuck notes.
            if (needsDisableNoteOff_) {
                emitPanicNoteOffs(outputEvents, eventCount, maxEvents);
            }
            return eventCount;
        }

        // Retrigger Beat: detect bar boundary within this block (FR-023)
        // Compute once before the loop; store as samples-from-block-start.
        size_t barBoundaryOffset = SIZE_MAX;  // SIZE_MAX = no boundary
        if (retriggerMode_ == ArpRetriggerMode::Beat) {
            barBoundaryOffset = detectBarBoundary(ctx);
        }

        // Jump-ahead main loop
        size_t samplesProcessed = 0;
        const size_t blockSize = ctx.blockSize;

        while (samplesProcessed < blockSize && eventCount < maxEvents) {
            // How many samples until next step boundary?
            size_t samplesUntilStep = currentStepDuration_ - sampleCounter_;

            // Find minimum pending NoteOff samplesRemaining (relative to
            // samplesProcessed position)
            size_t samplesUntilNoteOff = SIZE_MAX;
            for (size_t i = 0; i < pendingNoteOffCount_; ++i) {
                samplesUntilNoteOff =
                    std::min(samplesUntilNoteOff, pendingNoteOffs_[i].samplesRemaining);
            }

            // Retrigger Beat: how many samples until bar boundary?
            size_t samplesUntilBar = SIZE_MAX;
            if (barBoundaryOffset != SIZE_MAX &&
                barBoundaryOffset >= samplesProcessed) {
                samplesUntilBar = barBoundaryOffset - samplesProcessed;
            }

            // Jump to the minimum of (step boundary, pending NoteOff,
            // bar boundary, block end)
            size_t samplesUntilBlockEnd = blockSize - samplesProcessed;
            size_t jump = samplesUntilBlockEnd;  // default: consume to block end

            // 074-ratcheting: sub-step boundary timing (FR-014)
            // 078-ratchet-swing: use per-sub-step duration from array
            size_t samplesUntilSubStep = SIZE_MAX;
            if (ratchetSubStepsRemaining_ > 0) {
                samplesUntilSubStep = ratchetSubStepDurations_[ratchetSubStepIndex_] - ratchetSubStepCounter_;
            }

            // Determine which event fires first
            // Priority: BarBoundary > NoteOff > Step > SubStep (FR-014)
            enum class NextEvent : uint8_t { BlockEnd, NoteOff, Step, SubStep, BarBoundary };
            NextEvent next = NextEvent::BlockEnd;

            if (samplesUntilStep <= jump) {
                jump = samplesUntilStep;
                next = NextEvent::Step;
            }
            // SubStep: lower priority than Step (FR-014). The equal-distance
            // case is only taken when nothing else claimed this position, so a
            // Step at the same offset still wins.
            if (samplesUntilSubStep < jump ||
                (samplesUntilSubStep == jump && next == NextEvent::BlockEnd)) {
                jump = samplesUntilSubStep;
                next = NextEvent::SubStep;
            }
            if (samplesUntilNoteOff < jump ||
                (samplesUntilNoteOff == jump && next != NextEvent::Step)) {
                // NoteOff fires before or at same time as step (NoteOff before
                // NoteOn at same offset per FR-021)
                jump = samplesUntilNoteOff;
                next = NextEvent::NoteOff;
            }
            // If NoteOff and Step fire at the same sample, process NoteOff first
            // (FR-021 event ordering)
            if (samplesUntilNoteOff == samplesUntilStep &&
                samplesUntilStep <= samplesUntilBlockEnd) {
                next = NextEvent::NoteOff;
            }
            // If NoteOff and SubStep fire at the same sample, process NoteOff first
            if (samplesUntilNoteOff == samplesUntilSubStep &&
                samplesUntilSubStep <= samplesUntilBlockEnd) {
                next = NextEvent::NoteOff;
            }
            // Bar boundary: must fire before or at same time as step
            if (samplesUntilBar < jump) {
                jump = samplesUntilBar;
                next = NextEvent::BarBoundary;
            } else if (samplesUntilBar == jump) {
                // Bar boundary coincident with anything else: the bar wins, per
                // the documented BarBoundary > NoteOff > Step > SubStep order.
                //
                // This used to exclude next == NoteOff, which the overrides
                // above force whenever a due NoteOff lands on the step boundary
                // -- exactly what a ~100% gate produces. Control then entered
                // the NoteOff handler, found sampleCounter_ >= currentStepDuration_
                // and fired the step WITHOUT selector_.reset()/resetLanes(),
                // while barBoundaryOffset is only invalidated inside the
                // BarBoundary branch. The reset was deferred to the next
                // iteration, so with Retrigger=Beat the first step of every bar
                // played the previous bar's continuation instead of step 0.
                //
                // Taking the BarBoundary branch is safe for FR-021 ordering: it
                // emits due NoteOffs (below) before calling fireStep.
                next = NextEvent::BarBoundary;
            }

            // Advance time by jump amount
            sampleCounter_ += jump;
            samplesProcessed += jump;
            decrementPendingNoteOffs(jump);
            // 074-ratcheting: advance sub-step counter (FR-016)
            if (ratchetSubStepsRemaining_ > 0) {
                ratchetSubStepCounter_ += jump;
            }

            if (next == NextEvent::BlockEnd) {
                // No events fire -- we consumed the rest of the block
                break;
            }

            int32_t sampleOffset = static_cast<int32_t>(samplesProcessed);

            // Guard: if event fires exactly at blockSize, defer to next block
            if (samplesProcessed >= blockSize) {
                break;
            }

            if (next == NextEvent::BarBoundary) {
                // Bar boundary: reset selector and swing counter (FR-023)
                selector_.reset();
                swingStepCounter_ = 0;
                resetLanes();
                // Invalidate bar boundary so it doesn't fire again this block
                barBoundaryOffset = SIZE_MAX;

                // Emit any pending NoteOffs that are due at this sample
                emitDuePendingNoteOffs(sampleOffset, outputEvents, eventCount,
                                       maxEvents);

                // If step boundary also fires at this exact sample, process it
                if (sampleCounter_ >= currentStepDuration_) {
                    sampleCounter_ = 0;
                    // Recalculate step duration after swing reset
                    currentStepDuration_ = calculateStepDuration(ctx);
                    fireStep(ctx, sampleOffset, outputEvents, eventCount,
                             maxEvents, samplesProcessed, blockSize);
                } else {
                    // Recalculate current step duration with reset swing counter.
                    // sampleCounter_ is intentionally left alone: the elapsed
                    // count carries over against the new duration. That is safe
                    // because swingStepCounter_ was just reset to 0, so the
                    // recomputed duration is the LONG half of the swing pair and
                    // therefore >= the duration the counter was accumulating
                    // against -- samplesUntilStep (currentStepDuration_ -
                    // sampleCounter_) cannot underflow.
                    currentStepDuration_ = calculateStepDuration(ctx);
                }
            } else if (next == NextEvent::NoteOff) {
                // Emit all pending NoteOffs that are due at this sample
                emitDuePendingNoteOffs(sampleOffset, outputEvents, eventCount, maxEvents);

                // After emitting NoteOffs, check if step boundary also fires
                // at this exact sample
                if (sampleCounter_ >= currentStepDuration_) {
                    // Step fires at same offset -- process it
                    sampleCounter_ = 0;
                    // 074-ratcheting: recalculate before fireStep (FR-025)
                    currentStepDuration_ = calculateStepDuration(ctx);
                    fireStep(ctx, sampleOffset, outputEvents, eventCount, maxEvents,
                             samplesProcessed, blockSize);
                }
                // 074-ratcheting: NoteOff-coincident SubStep check (FR-015)
                // 078-ratchet-swing: use per-sub-step duration from array
                else if (ratchetSubStepsRemaining_ > 0 &&
                         ratchetSubStepCounter_ >= ratchetSubStepDurations_[ratchetSubStepIndex_]) {
                    fireSubStep(ctx, sampleOffset, outputEvents, eventCount, maxEvents);
                }
            } else if (next == NextEvent::Step) {
                // Step boundary reached
                sampleCounter_ = 0;
                // 074-ratcheting: Recalculate step duration before fireStep.
                // The previous step's fireStep() already incremented swingStepCounter_
                // but may have deferred the recalculation (when ratchetCount > 1).
                // This ensures fireStep() sees the correct base duration for
                // sub-step calculations. (FR-025)
                currentStepDuration_ = calculateStepDuration(ctx);

                // First emit any pending NoteOffs that are also due at this sample
                emitDuePendingNoteOffs(sampleOffset, outputEvents, eventCount, maxEvents);

                fireStep(ctx, sampleOffset, outputEvents, eventCount, maxEvents,
                         samplesProcessed, blockSize);
            } else if (next == NextEvent::SubStep) {
                // 074-ratcheting: Sub-step boundary reached (FR-015)
                fireSubStep(ctx, sampleOffset, outputEvents, eventCount, maxEvents);
            }
        }

        // Decrement remaining pending NoteOffs by the unused portion of the block
        // (already handled by the jump-ahead loop consuming all samples)
        return eventCount;
    }

void ArpeggiatorCore::fireSubStep([[maybe_unused]] const BlockContext& ctx,
                             int32_t sampleOffset,
                             std::span<ArpEvent> outputEvents,
                             size_t& eventCount,
                             size_t maxEvents) noexcept {
        // (1) Emit pending NoteOffs due at this sample offset FIRST (FR-021 ordering)
        emitDuePendingNoteOffs(sampleOffset, outputEvents, eventCount, maxEvents);

        // 078-ratchet-swing: advance sub-step index to the sub-step being fired
        ++ratchetSubStepIndex_;

        // v1.5: Ratchet Velocity Decay — each sub-step applies decay^n to velocity.
        // ratchetDecay_ is 0.0-1.0 (UI 0-100%). factor = (1.0 - decay)^subStepIndex.
        // subStepIndex 1 = second sub-step (first subdivision is emitted by the normal
        // step emission path, not fireSubStep).
        const float decayFactor = (ratchetDecay_ > 0.0f)
            ? std::pow(1.0f - ratchetDecay_, static_cast<float>(ratchetSubStepIndex_))
            : 1.0f;

        // Floor at 1, matching the main step path (which clamps to [1,127]).
        // A NoteOn with velocity 0 is a note-off to many hosts, so a fully
        // decayed sub-step would silence the note early at data.outputEvents
        // while the audition voice read it as active-but-silent -- host and
        // local monitor disagreeing about what is sounding.
        auto applyDecay = [decayFactor](uint8_t v) -> uint8_t {
            float scaled = static_cast<float>(v) * decayFactor;
            return static_cast<uint8_t>(std::clamp(scaled, 1.0f, 127.0f));
        };

        // (2) Emit noteOn for ratcheted note(s)
        if (ratchetNoteCount_ > 1) {
            // Chord mode: emit noteOn for all chord notes
            // v1.5: Reuse strum offsets from main fireStep path for direction consistency
            for (size_t i = 0; i < ratchetNoteCount_ && eventCount < maxEvents; ++i) {
                outputEvents[eventCount++] = ArpEvent{
                    .type = ArpEvent::Type::NoteOn,
                    .note = ratchetNotes_[i],
                    .velocity = applyDecay(ratchetVelocities_[i]),
                    .sampleOffset = clampOffsetToBlock(
                        sampleOffset + strumOffsetFor(i), ctx.blockSize),
                    .legato = false};  // Sub-steps after first are never legato
            }
            // Update currentArpNotes_ tracking
            for (size_t i = 0; i < ratchetNoteCount_ && i < 32; ++i) {
                currentArpNotes_[i] = ratchetNotes_[i];
            }
            currentArpNoteCount_ = ratchetNoteCount_ < 32 ? ratchetNoteCount_ : 32;
        } else {
            // Single note mode
            if (eventCount < maxEvents) {
                outputEvents[eventCount++] = ArpEvent{
                    .type = ArpEvent::Type::NoteOn,
                    .note = ratchetNote_,
                    .velocity = applyDecay(ratchetVelocity_),
                    .sampleOffset = sampleOffset,
                    .legato = false};  // Sub-steps after first are never legato
            }
            currentArpNotes_[0] = ratchetNote_;
            currentArpNoteCount_ = 1;
        }

        // (3) Schedule pending noteOff using per-sub-step gate duration
        // 078-ratchet-swing: use ratchetGateDurations_[ratchetSubStepIndex_]
        // For the last sub-step, check look-ahead: if next step is Tie/Slide,
        // suppress the gate noteOff so the note sustains into the next step (FR-022).
        // Sub-steps 0 through N-2 always schedule their noteOffs normally.
        bool suppressGateNoteOff = false;
        if (ratchetIsLastSubStep_) {
            uint8_t nextModFlags = modifierLane_.getStep(modifierLane_.currentStep());
            bool nextStepIsTie = (nextModFlags & kStepActive) != 0 &&
                                 (nextModFlags & kStepTie) != 0;
            bool nextStepIsSlide = (nextModFlags & kStepActive) != 0 &&
                                   (nextModFlags & kStepSlide) != 0;
            suppressGateNoteOff = nextStepIsTie || nextStepIsSlide;
        }

        const size_t subGate = ratchetGateDurations_[ratchetSubStepIndex_];
        if (!suppressGateNoteOff) {
            if (ratchetNoteCount_ > 1) {
                // v1.5: Apply strum offset to each chord note's gate duration
                for (size_t i = 0; i < ratchetNoteCount_; ++i) {
                    const size_t strumOff = static_cast<size_t>(strumOffsetFor(i));
                    addPendingNoteOff(ratchetNotes_[i], subGate + strumOff,
                                       outputEvents, eventCount, maxEvents);
                }
            } else {
                addPendingNoteOff(ratchetNote_, subGate,
                                   outputEvents, eventCount, maxEvents);
            }
        }

        // (4) Decrement remaining count and reset counter
        --ratchetSubStepsRemaining_;
        ratchetSubStepCounter_ = 0;

        // Update last-sub-step flag for next sub-step
        ratchetIsLastSubStep_ = (ratchetSubStepsRemaining_ == 1);
    }

void ArpeggiatorCore::fireStep(const BlockContext& ctx,
                          int32_t sampleOffset,
                          std::span<ArpEvent> outputEvents,
                          size_t& eventCount,
                          size_t maxEvents,
                          [[maybe_unused]] size_t samplesProcessed,
                          [[maybe_unused]] size_t blockSize) noexcept {
        // Spec 142 T030b/T030c: in Sequencer mode the source pitch comes from
        // lane 9 (seqNoteLane_), bypassing ArpMode/Octave/Markov/etc. The
        // selector_ is NOT consulted at all — it would mutate internal state
        // that is irrelevant to Seq mode. Transposition root + base velocity
        // are read from heldNotes_ (single source of truth across both modes).
        const bool seqMode = (sourceMode_ == SourceMode::Sequencer);
        bool seqRestStep = false;
        ArpNoteResult result;
        if (seqMode) {
            const size_t seqStep = seqNoteLane_.currentStep();
            const uint8_t programmedPitch = seqNoteLane_.currentValue();
            seqRestStep = (seqStep < 32) && (seqRestFlags_[seqStep].load(
                std::memory_order_relaxed) != 0);

            auto held = heldNotes_.byInsertOrder();
            int heldRoot = 60;     // FR-018: default = no transposition
            uint8_t baseVel = 100; // FR-025a: default base velocity
            if (!held.empty()) {
                heldRoot = static_cast<int>(held.back().note);
                baseVel = held.back().velocity;
            }
            // T030c: transposition formula uses heldRoot-60. Output scale
            // quantize and pitch lane / kArpTranspose are stacked downstream.
            //
            // Note (spec 142 Phase 3 compliance): this clamp runs BEFORE the
            // pitch lane and global kArpTranspose stages, each of which
            // applies its own [0,127] clamp on the final emitted pitch. The
            // per-stage clamping mirrors Live mode's behavior (the pitch lane
            // and global transpose clamp independently in Live too) and is
            // acceptable per FR-024 ("clamped per existing Gradus output-clamp
            // behavior — consistent with how the pitch lane behaves in Live
            // mode today"). FR-024 only mandates that the final emitted pitch
            // be in range, which the downstream stages guarantee.
            int transposed = std::clamp(
                static_cast<int>(programmedPitch) + (heldRoot - 60), 0, 127);
            result.notes[0] = static_cast<uint8_t>(transposed);
            result.velocities[0] = baseVel;
            result.count = 1;  // always emit one — rest is handled below via
                               // the modifier-rest path so all lanes advance.
        } else {
            // Live mode: original behavior — advance the selector against held notes.
            result = selector_.advance(heldNotes_);
        }

        if (result.count > 0) {
            // v1.5 Part 3: Step Pinning — if the current pitch-lane step is
            // pinned, override the arp-pattern note(s) with the global pin note.
            // Indexed by the pitch lane's current step (pitch lane is the natural
            // "pattern position" for pitch overrides).
            // FR-022: pin note is inert in Sequencer mode — pattern owns ordering.
            const size_t pitchStepIdx = pitchLane_.currentStep();
            const bool isPinnedStep = (pitchStepIdx < 32 && pinFlags_[pitchStepIdx])
                                       && !seqMode;
            if (isPinnedStep) {
                // Collapse to a single pinned note (chord expansion disabled for pinned steps)
                result.notes[0] = pinNote_;
                result.count = 1;
            }

            // 077-spice-dice-humanize: capture overlay indices BEFORE lane advances (FR-010)
            const size_t velStep = velocityLane_.currentStep();
            const size_t gateStep = gateLane_.currentStep();
            const size_t ratchetStep = ratchetLane_.currentStep();
            const size_t condStep = conditionLane_.currentStep();

            // Read current lane values BEFORE speed-based advancement
            float velScale = velocityLane_.currentValue();
            float gateScale = gateLane_.currentValue();
            int8_t pitchOffset = pitchLane_.currentValue();
            uint8_t modifierFlags = modifierLane_.currentValue();
            uint8_t ratchetCount = std::max(uint8_t{1}, ratchetLane_.currentValue());
            uint8_t condValue = conditionLane_.currentValue();
            uint8_t chordTypeVal = chordLane_.currentValue();
            uint8_t inversionVal = inversionLane_.currentValue();

            // Advance lanes using per-lane speed accumulators with per-lane swing
            // v1.5: Per-lane swing skews the advance threshold — even advances
            // take (1+swing) ticks, odd advances take (1-swing) ticks.
            // v1.5 Part 2: Length Jitter re-rolls pattern length on wrap.
            auto advanceLaneBySpeed = [this](auto& lane, size_t laneIdx) {
                float& accum = laneAccumulators_[laneIdx];
                float speed = laneSpeedMultipliers_[laneIdx];

                // Apply speed curve modulation: curve offsets the center speed.
                // Depth is written from the message thread, so load it once and
                // use that snapshot for both the gate and the offset -- two
                // separate loads could straddle a write and disagree.
                const float curveDepth =
                    laneSpeedCurveDepths_[laneIdx].load(std::memory_order_relaxed);
                if (laneSpeedCurveEnabled_[laneIdx].load(std::memory_order_relaxed) &&
                    curveDepth > 0.0f) {
                    float loopPos = static_cast<float>(lane.currentStep())
                                  / std::max(1.0f, static_cast<float>(lane.length()));
                    int tableIdx = std::clamp(
                        static_cast<int>(loopPos * 255.0f), 0, 255);
                    float curveVal = laneSpeedCurveTables_[laneIdx]
                                     [static_cast<size_t>(tableIdx)];
                    // curveVal 0.5 = no offset, 0.0 = -depth, 1.0 = +depth
                    float offset = (curveVal - 0.5f) * 2.0f * curveDepth;
                    speed *= (1.0f + offset);
                    speed = std::clamp(speed, 0.1f, 8.0f);
                }

                const float swing = laneSwingAmounts_[laneIdx];
                uint8_t& counter = laneSwingCounters_[laneIdx];

                accum += speed;
                // Swing lengthens even-numbered sub-steps and shortens odd ones.
                float threshold = 1.0f;
                if (swing > 0.0f) {
                    threshold = ((counter & 1u) == 0u) ? 1.0f + swing : 1.0f - swing;
                }

                while (accum >= threshold) {
                    accum -= threshold;

                    // v1.5: Per-lane length jitter — skip this advance if pending skips
                    if (lanePendingSkips_[laneIdx] > 0) {
                        --lanePendingSkips_[laneIdx];
                    } else {
                        const int prevStep = static_cast<int>(lane.currentStep());
                        lane.advance();
                        const int newStep = static_cast<int>(lane.currentStep());

                        // Detect wrap (new step < previous step)
                        const int laneJitter = laneLengthJitters_[laneIdx];
                        if (laneJitter > 0 && newStep < prevStep) {
                            uint32_t& s = lengthJitterRng_;
                            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                            const int range = 2 * laneJitter + 1;
                            const int jitter =
                                static_cast<int>(s % static_cast<uint32_t>(range))
                                - laneJitter;
                            if (jitter > 0) {
                                // Extra advances now: shorten this cycle.
                                // Also increment the swing counter for each so
                                // per-lane swing phase stays aligned with the
                                // actual step position.
                                for (int j = 0; j < jitter; ++j) {
                                    lane.advance();
                                    ++counter;
                                }
                            } else if (jitter < 0) {
                                // Schedule skips: lengthen next cycle
                                lanePendingSkips_[laneIdx] =
                                    static_cast<int8_t>(-jitter);
                            }
                        }
                    }

                    ++counter;
                    if (swing > 0.0f) {
                        threshold = (counter & 1u) == 0u
                            ? 1.0f + swing
                            : 1.0f - swing;
                    }
                }
            };
            advanceLaneBySpeed(velocityLane_,  0);
            advanceLaneBySpeed(gateLane_,      1);
            advanceLaneBySpeed(pitchLane_,     2);
            advanceLaneBySpeed(modifierLane_,  3);
            advanceLaneBySpeed(ratchetLane_,   4);
            advanceLaneBySpeed(conditionLane_, 5);
            advanceLaneBySpeed(chordLane_,     6);
            advanceLaneBySpeed(inversionLane_, 7);
            advanceLaneBySpeed(midiDelayLane_, 8);
            // Spec 142 T030a: lane 9 (Sequencer Note) advances ONLY in Seq mode.
            // In Live mode the lane is fully inert — neither advances nor emits,
            // preserving byte-identical Live MIDI (SC-004, SC-004b).
            if (sourceMode_ == SourceMode::Sequencer) {
                advanceLaneBySpeed(seqNoteLane_, 9);
            }

            // 077-spice-dice-humanize: apply Spice blend (FR-008, FR-009)
            if (spice_ > 0.0f) {
                // Velocity: linear interpolation (FR-009)
                velScale = velScale + (velocityOverlay_[velStep] - velScale) * spice_;
                // Gate: linear interpolation
                gateScale = gateScale + (gateOverlay_[gateStep] - gateScale) * spice_;
                // Ratchet: lerp + round to integer (FR-008)
                float ratchetBlend = static_cast<float>(ratchetCount)
                    + (static_cast<float>(ratchetOverlay_[ratchetStep])
                       - static_cast<float>(ratchetCount)) * spice_;
                ratchetCount = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::round(ratchetBlend)), 1, 4));
                // Condition: threshold blend (FR-008)
                if (spice_ >= 0.5f) {
                    condValue = conditionOverlay_[condStep];
                }
            }

            // 076-conditional-trigs: detect loop count wrap (FR-011)
            // After advance, if position wrapped back to 0, the lane completed one cycle.
            // For length 1, this fires on every step (correct per FR-018).
            // The actual loopCount_ increment is deferred to AFTER condition evaluation
            // so that the current step evaluates against the pre-increment loopCount_.
            // This ensures First (loopCount_ == 0) fires on the very first step.
            const bool conditionLaneWrapped = (conditionLane_.currentStep() == 0);

            // --- 075-euclidean-timing: Euclidean gating check (FR-011, FR-012) ---
            // Evaluated AFTER all lane advances but BEFORE modifier evaluation.
            // All lanes above advance unconditionally on every step tick,
            // including Euclidean rest steps (FR-004, FR-011).
            // Spec 142 FR-022: Euclidean is inert in Sequencer mode (the
            // pattern owns hit timing; Euclidean gating would silence steps).
            if (euclideanEnabled_ && !seqMode) {
                bool isHitStep = EuclideanPattern::isHit(
                    euclideanPattern_,
                    static_cast<int>(euclideanPosition_),
                    euclideanSteps_);

                // Advance position unconditionally (FR-012)
                euclideanPosition_ = (euclideanPosition_ + 1)
                    % static_cast<size_t>(euclideanSteps_);

                if (!isHitStep) {
                    // Euclidean rest path (FR-004, FR-006, FR-007):
                    // Cancel pending noteOffs to prevent double emission
                    cancelPendingNoteOffsForCurrentNotes();

                    // Emit noteOff for all currently sounding notes
                    for (size_t i = 0; i < currentArpNoteCount_ && eventCount < maxEvents; ++i) {
                        outputEvents[eventCount++] = ArpEvent{
                            .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[i], .velocity = 0, .sampleOffset = sampleOffset};
                    }
                    currentArpNoteCount_ = 0;

                    // Break any active tie chain (FR-007)
                    tieActive_ = false;

                    // Increment swing step counter and recalculate duration
                    ++swingStepCounter_;
                    currentStepDuration_ = calculateStepDuration(ctx);

                    // 076-conditional-trigs: deferred loopCount_ increment (FR-011)
                    // Must happen even on Euclidean rest (condition not evaluated but
                    // loop counter still tracks lane cycles).
                    if (conditionLaneWrapped) {
                        ++loopCount_;
                    }
                    // 077-spice-dice-humanize: consume humanize PRNG on skipped step (FR-023)
                    (void)humanizeRng_.nextFloat();  // timing (discarded)
                    (void)humanizeRng_.nextFloat();  // velocity (discarded)
                    (void)humanizeRng_.nextFloat();  // gate (discarded)

                    // 081-interaction-polish: emit kSkip event (FR-007, FR-008)
                    if (eventCount < maxEvents) {
                        outputEvents[eventCount++] = ArpEvent{
                            .type = ArpEvent::Type::kSkip,
                            .note = static_cast<uint8_t>(velStep), .velocity = 0, .sampleOffset = sampleOffset};
                    }
                    return;
                }
            }

            // --- 076-conditional-trigs: Condition evaluation (FR-012, FR-014) ---
            // Evaluated AFTER Euclidean gating but BEFORE modifier evaluation.
            // If condition fails, treat as rest (identical to Euclidean rest path).
            if (!evaluateCondition(condValue)) {
                // Condition-fail rest path: identical to Euclidean rest path
                cancelPendingNoteOffsForCurrentNotes();

                // Emit noteOff for all currently sounding notes
                for (size_t i = 0; i < currentArpNoteCount_ && eventCount < maxEvents; ++i) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[i], .velocity = 0, .sampleOffset = sampleOffset};
                }
                currentArpNoteCount_ = 0;

                // Break any active tie chain (FR-029)
                tieActive_ = false;

                // Increment swing step counter and recalculate duration
                ++swingStepCounter_;
                currentStepDuration_ = calculateStepDuration(ctx);

                // 076-conditional-trigs: deferred loopCount_ increment (FR-011)
                if (conditionLaneWrapped) {
                    ++loopCount_;
                }
                // 077-spice-dice-humanize: consume humanize PRNG on skipped step (FR-023)
                (void)humanizeRng_.nextFloat();  // timing (discarded)
                (void)humanizeRng_.nextFloat();  // velocity (discarded)
                (void)humanizeRng_.nextFloat();  // gate (discarded)

                // 081-interaction-polish: emit kSkip event (FR-007, FR-008)
                if (eventCount < maxEvents) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::kSkip,
                        .note = static_cast<uint8_t>(velStep), .velocity = 0, .sampleOffset = sampleOffset};
                }
                return;
            }

            // 076-conditional-trigs: deferred loopCount_ increment (FR-011)
            // Condition passed -- increment loop count if the lane wrapped this step.
            if (conditionLaneWrapped) {
                ++loopCount_;
            }

            // --- Modifier evaluation (073-per-step-mods) ---
            // Priority: Rest > Tie > Slide > Accent
            // Spec 142 (T030b): a Sequencer-mode rest step is treated as a
            // modifier rest — the noteOn is suppressed, any sounding note
            // emits its noteOff, and the playhead continues to advance for
            // all lanes (FR-019, SC-008).

            // Rest: kStepActive not set -> suppress noteOn, emit noteOff for previous
            if (seqRestStep || !(modifierFlags & kStepActive)) {
                // Cancel pending noteOffs first to prevent double NoteOff emission
                // (emitDuePendingNoteOffs would otherwise re-emit for these notes)
                cancelPendingNoteOffsForCurrentNotes();

                // Emit noteOff for all currently sounding notes
                for (size_t i = 0; i < currentArpNoteCount_ && eventCount < maxEvents; ++i) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[i], .velocity = 0, .sampleOffset = sampleOffset};
                }
                currentArpNoteCount_ = 0;
                tieActive_ = false;

                // Increment swing step counter and recalculate duration
                ++swingStepCounter_;
                currentStepDuration_ = calculateStepDuration(ctx);
                // 077-spice-dice-humanize: consume humanize PRNG on skipped step (FR-023)
                (void)humanizeRng_.nextFloat();  // timing (discarded)
                (void)humanizeRng_.nextFloat();  // velocity (discarded)
                (void)humanizeRng_.nextFloat();  // gate (discarded)

                // 081-interaction-polish: emit kSkip event (FR-007, FR-008)
                if (eventCount < maxEvents) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::kSkip,
                        .note = static_cast<uint8_t>(velStep), .velocity = 0, .sampleOffset = sampleOffset};
                }
                return;
            }

            // Tie: kStepTie set -> sustain previous notes, no new noteOn (FR-011)
            if (modifierFlags & kStepTie) {
                if (currentArpNoteCount_ > 0) {
                    // Cancel pending noteOffs: tie overrides gate (FR-012)
                    cancelPendingNoteOffsForCurrentNotes();
                    tieActive_ = true;

                    // Increment swing step counter and recalculate duration
                    ++swingStepCounter_;
                    currentStepDuration_ = calculateStepDuration(ctx);
                    // 077-spice-dice-humanize: consume humanize PRNG on skipped step (FR-023, FR-024)
                    (void)humanizeRng_.nextFloat();  // timing (discarded)
                    (void)humanizeRng_.nextFloat();  // velocity (discarded)
                    (void)humanizeRng_.nextFloat();  // gate (discarded)
                    return;
                }
                // No preceding note -> behaves as rest (FR-013)
                tieActive_ = false;
                ++swingStepCounter_;
                currentStepDuration_ = calculateStepDuration(ctx);
                // 077-spice-dice-humanize: consume humanize PRNG on skipped step (FR-023, FR-024)
                (void)humanizeRng_.nextFloat();  // timing (discarded)
                (void)humanizeRng_.nextFloat();  // velocity (discarded)
                (void)humanizeRng_.nextFloat();  // gate (discarded)
                return;
            }

            // Active step (not Rest, not Tie): end any tie chain (FR-014)
            // If ending a tie chain, emit noteOffs for tie-sustained notes
            // before normal note emission (their pending noteOffs were cancelled).
            if (tieActive_) {
                for (size_t i = 0; i < currentArpNoteCount_ && eventCount < maxEvents; ++i) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[i], .velocity = 0, .sampleOffset = sampleOffset};
                }
                currentArpNoteCount_ = 0;
            }
            tieActive_ = false;

            // Slide evaluation (FR-015, FR-016, FR-017)
            // Determine if this is a slide step with a preceding sounding note
            bool isSlide = (modifierFlags & kStepSlide) != 0 && currentArpNoteCount_ > 0;

            // Apply velocity scaling to all notes in this step (FR-011)
            for (size_t i = 0; i < result.count; ++i) {
                int scaledVel = static_cast<int>(
                    std::round(result.velocities[i] * velScale));
                result.velocities[i] = static_cast<uint8_t>(
                    std::clamp(scaledVel, 1, 127));
            }

            // v1.5: Apply velocity curve (after lane scaling, before accent)
            if (velocityCurveAmount_ > 0.0f && velocityCurveType_ != 0) {
                for (size_t i = 0; i < result.count; ++i) {
                    float normalized = static_cast<float>(result.velocities[i]) / 127.0f;
                    float curved = applyVelocityCurve(normalized);
                    // Blend linear and curved by amount
                    float blended = normalized + (curved - normalized) * velocityCurveAmount_;
                    result.velocities[i] = static_cast<uint8_t>(
                        std::clamp(static_cast<int>(std::round(blended * 127.0f)), 1, 127));
                }
            }

            // 074-ratcheting (FR-020): Capture pre-accent velocities for
            // subsequent sub-steps. Accent applies to first sub-step only;
            // remaining sub-steps use these un-accented velocities.
            std::array<uint8_t, 32> preAccentVelocities{};
            for (size_t i = 0; i < result.count && i < 32; ++i) {
                preAccentVelocities[i] = result.velocities[i];
            }

            // Accent: boost velocity after lane scaling (FR-019, FR-020)
            // Accent applies to any step that emits a noteOn (Active and Slide).
            // Rest and Tie return early above, so this code only runs for note-emitting steps.
            if ((modifierFlags & kStepAccent) && accentVelocity_ > 0) {
                for (size_t i = 0; i < result.count; ++i) {
                    int boosted = static_cast<int>(result.velocities[i]) + accentVelocity_;
                    result.velocities[i] = static_cast<uint8_t>(
                        std::clamp(boosted, 1, 127));
                }
            }

            // =================================================================
            // arp-chord-lane: Chord expansion
            // =================================================================
            // Expand single-note result to chord BEFORE pitch offset.
            // Skipped when ArpMode is Chord (already playing all held notes).
            // Skipped when chord type is None (default single-note behavior).
            auto chordType = static_cast<ChordType>(
                std::clamp(static_cast<int>(chordTypeVal), 0,
                           static_cast<int>(ChordType::kCount) - 1));
            auto invType = static_cast<InversionType>(
                std::clamp(static_cast<int>(inversionVal), 0,
                           static_cast<int>(InversionType::kCount) - 1));

            if (chordType != ChordType::None &&
                arpMode_ != ArpMode::Chord &&
                result.count == 1) {
                // Generate chord from the single base note
                ChordResult chord = generateChordNotes(
                    result.notes[0], chordType, scaleHarmonizer_);
                applyInversion(chord, invType);
                applyVoicing(chord, voicingMode_,
                    static_cast<uint32_t>(sampleCounter_ ^ swingStepCounter_));

                // Expand result to contain all chord notes
                // Use same velocity for all chord tones
                uint8_t baseVelocity = result.velocities[0];
                for (size_t ci = 0; ci < chord.count && ci < 32; ++ci) {
                    result.notes[ci] = chord.notes[ci];
                    result.velocities[ci] = baseVelocity;
                }
                result.count = chord.count < 32 ? chord.count : 32;
            }

            // Apply pitch offset to all notes in this step (FR-005, FR-006, FR-008, 084-arp-scale-mode)
            // v1.5: Skipped for pinned steps (pinned note bypasses pitch processing)
            if (!isPinnedStep) {
                for (size_t i = 0; i < result.count; ++i) {
                    if (scaleHarmonizer_.getScale() != ScaleType::Chromatic && pitchOffset != 0) {
                        // Scale mode: interpret pitchOffset as scale degrees
                        auto interval = scaleHarmonizer_.calculate(
                            static_cast<int>(result.notes[i]),
                            static_cast<int>(pitchOffset));
                        result.notes[i] = static_cast<uint8_t>(
                            std::clamp(interval.targetNote, 0, 127));
                    } else {
                        // Chromatic mode or zero offset: direct semitone addition
                        int offsetNote = static_cast<int>(result.notes[i]) +
                                         static_cast<int>(pitchOffset);
                        result.notes[i] = static_cast<uint8_t>(
                            std::clamp(offsetNote, 0, 127));
                    }
                }
            }

            // v1.5: Apply global Transpose. Semantics match the existing pitch
            // lane (spec 084-arp-scale-mode): in Chromatic mode the value is
            // treated as semitones; in scale mode it's treated as scale degrees
            // ("steps") so the result always stays diatonic. The spec example
            // "+2 in C major = C→D→E" confirms scale-degree semantics when a
            // scale is active (C + 2 degrees = E).
            // v1.5: Transpose skipped for pinned steps
            if (transpose_ != 0 && !isPinnedStep) {
                for (size_t i = 0; i < result.count; ++i) {
                    if (scaleHarmonizer_.getScale() != ScaleType::Chromatic) {
                        auto interval = scaleHarmonizer_.calculate(
                            static_cast<int>(result.notes[i]),
                            transpose_);
                        result.notes[i] = static_cast<uint8_t>(
                            std::clamp(interval.targetNote, 0, 127));
                    } else {
                        // Chromatic mode: direct semitone addition
                        int tn = static_cast<int>(result.notes[i]) + transpose_;
                        result.notes[i] = static_cast<uint8_t>(std::clamp(tn, 0, 127));
                    }
                }
            }

            // v1.5 Part 3: Apply Note Range Mapping (after all pitch processing)
            // Mode 0=Wrap, 1=Clamp, 2=Skip (drop notes that fall outside).
            // Pinned steps bypass range mapping — the pin note is the user's
            // explicit target and should not be folded/clamped.
            // Spec 142 FR-022: Range Mapping is inert in Sequencer mode — the
            // pattern is the source, not held-input mapping.
            if (!isPinnedStep && !seqMode && (rangeLow_ > 0 || rangeHigh_ < 127)) {
                const int lo = std::min(rangeLow_, rangeHigh_);
                const int hi = std::max(rangeLow_, rangeHigh_);
                const int span = hi - lo + 1;
                size_t writeIdx = 0;
                for (size_t i = 0; i < result.count; ++i) {
                    int note = static_cast<int>(result.notes[i]);
                    bool keep = true;
                    if (note < lo || note > hi) {
                        switch (rangeMode_) {
                            case 0: // Wrap
                                if (span > 0) {
                                    int offset = ((note - lo) % span + span) % span;
                                    note = lo + offset;
                                }
                                break;
                            case 1: // Clamp
                                note = std::clamp(note, lo, hi);
                                break;
                            case 2: // Skip
                                keep = false;
                                break;
                            default:
                                // Unknown mode: leave the note as-is rather than
                                // silently dropping or relocating it.
                                break;
                        }
                    }
                    if (keep) {
                        result.notes[writeIdx] = static_cast<uint8_t>(note);
                        if (writeIdx != i) {
                            result.velocities[writeIdx] = result.velocities[i];
                        }
                        ++writeIdx;
                    }
                }
                result.count = writeIdx;
            }

            // 077-spice-dice-humanize: Humanize offsets (FR-014, FR-022 steps 11-14)
            // Always consume 3 PRNG values for deterministic advancement (FR-018)
            const float timingRand = humanizeRng_.nextFloat();    // [-1, 1]
            const float velocityRand = humanizeRng_.nextFloat();  // [-1, 1]
            const float gateRand = humanizeRng_.nextFloat();      // [-1, 1]

            // Compute humanized timing offset (FR-015)
            const int32_t maxTimingOffsetSamples =
                static_cast<int32_t>(sampleRate_ * 0.020f);  // 20ms
            int32_t timingOffsetSamples =
                static_cast<int32_t>(timingRand * static_cast<float>(maxTimingOffsetSamples) * humanize_);
            int32_t humanizedSampleOffset = std::clamp(
                sampleOffset + timingOffsetSamples,
                static_cast<int32_t>(0),
                static_cast<int32_t>(blockSize) - 1);

            // Compute humanized velocity offset (FR-016)
            int velocityOffset = static_cast<int>(velocityRand * 15.0f * humanize_);
            // Apply to all notes in result (after accent)
            for (size_t i = 0; i < result.count; ++i) {
                int humanizedVel = static_cast<int>(result.velocities[i]) + velocityOffset;
                result.velocities[i] = static_cast<uint8_t>(std::clamp(humanizedVel, 1, 127));
            }

            // Compute humanized gate offset ratio (FR-017, FR-021)
            float gateOffsetRatio = gateRand * 0.10f * humanize_;

            // Calculate gate duration with lane multiplier (FR-014)
            size_t gateDuration = calculateGateDuration(gateScale);
            // Apply humanize gate offset (FR-017)
            {
                int32_t humanizedGateDuration = static_cast<int32_t>(gateDuration)
                    + static_cast<int32_t>(static_cast<float>(gateDuration) * gateOffsetRatio);
                gateDuration = static_cast<size_t>(std::max(int32_t{1}, humanizedGateDuration));
            }

            // GMF-009: gate length is measured from the note's ACTUAL onset.
            // addPendingNoteOff counts down from samplesProcessed -- the
            // un-humanized step position -- so without this delta every
            // positively-humanized note was cut short by exactly the humanize
            // offset, and once that offset exceeded the gate the release
            // scheduled BEFORE its own NoteOn: an orphan NoteOff, followed by a
            // note left with no scheduled release at all. Combined in int64 so a
            // negative delta cannot underflow the size_t, with a 1-sample floor.
            const auto scheduleGate = [&](uint8_t note, size_t base, size_t strumOff) {
                const int64_t total = static_cast<int64_t>(base)
                                    + static_cast<int64_t>(strumOff)
                                    + static_cast<int64_t>(humanizedSampleOffset)
                                    - static_cast<int64_t>(sampleOffset);
                addPendingNoteOff(note,
                                  total < 1 ? size_t{1} : static_cast<size_t>(total),
                                  outputEvents, eventCount, maxEvents);
            };

            // Peek at next modifier step: if the next step is a Tie or Slide step,
            // skip scheduling gate-based noteOffs so the notes sustain into
            // the next step (FR-012: Tie overrides gate; FR-015: Slide suppresses
            // previous noteOff to keep currentArpNoteCount_ > 0 for legato).
            uint8_t nextModFlags = modifierLane_.getStep(modifierLane_.currentStep());
            bool nextStepIsTie = (nextModFlags & kStepActive) != 0 &&
                                 (nextModFlags & kStepTie) != 0;
            bool nextStepIsSlide = (nextModFlags & kStepActive) != 0 &&
                                   (nextModFlags & kStepSlide) != 0;
            bool suppressGateNoteOff = nextStepIsTie || nextStepIsSlide;

            // =================================================================
            // 074-ratcheting: Ratchet subdivision (FR-007 through FR-013)
            // =================================================================
            if (ratchetCount > 1) {
                // 078-ratchet-swing: compute per-sub-step durations with swing
                computeSwungSubStepDurations(currentStepDuration_, ratchetCount,
                                             gateScale, gateOffsetRatio);
                ratchetSubStepIndex_ = 0;
                ratchetTotalSubSteps_ = ratchetCount;

                // Emit first sub-step (sub-step 0) in fireStep.
                // Remaining sub-steps emitted by processBlock SubStep handler.

                // Slide handling for first sub-step (FR-019)
                if (isSlide) {
                    cancelPendingNoteOffsForCurrentNotes();
                }

                // Emit noteOffs for currently sounding notes (replace previous)
                if (!isSlide) {
                    // Drop their scheduled gate NoteOffs first. With gate > 100%
                    // a pending off outlives the step that scheduled it, and
                    // addPendingNoteOff does not dedup, so a leftover would fire
                    // mid-step and cut the note this step re-strikes. Matches the
                    // rest/tie/slide paths, which all cancel before releasing.
                    cancelPendingNoteOffsForCurrentNotes();
                    size_t releasedCount = 0;
                    for (; releasedCount < currentArpNoteCount_ && eventCount < maxEvents;
                         ++releasedCount) {
                        outputEvents[eventCount++] = ArpEvent{
                            .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[releasedCount], .velocity = 0, .sampleOffset = humanizedSampleOffset};
                    }
                    // Keep whatever the span cap stopped us from releasing.
                    retainUnreleasedArpNotes(releasedCount);
                }

                // 077-spice-dice-humanize: Emit first sub-step noteOns at humanized offset (FR-019)
                // v1.5: Precompute strum offsets once per chord for consistent direction
                prepareStrumOffsets(result.count);
                size_t emittedNoteOns = 0;
                for (; emittedNoteOns < result.count && eventCount < maxEvents;
                     ++emittedNoteOns) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::NoteOn,
                        .note = result.notes[emittedNoteOns],
                        .velocity = result.velocities[emittedNoteOns],
                        .sampleOffset = clampOffsetToBlock(humanizedSampleOffset + strumOffsetFor(emittedNoteOns), blockSize),
                        .legato = isSlide};  // legato on first sub-step if Slide
                }

                // Track only the notes that actually sounded -- see the chord
                // path: tracking a dropped NoteOn produces a phantom release.
                // Appends, since the release loop above may have left notes.
                for (size_t i = 0; i < emittedNoteOns && currentArpNoteCount_ < 32; ++i) {
                    currentArpNotes_[currentArpNoteCount_++] = result.notes[i];
                }

                // Store ratchet state for remaining sub-steps (FR-011)
                ratchetSubStepCounter_ = 0;
                ratchetSubStepsRemaining_ = static_cast<uint8_t>(ratchetCount - 1);
                ratchetNoteCount_ = result.count < 32 ? result.count : 32;

                // Store note/velocity for sub-steps (FR-020: pre-accent velocity)
                // result.velocities has accent applied; subsequent sub-steps
                // use preAccentVelocities (velocity lane scaling only, no boost).
                if (result.count == 1) {
                    ratchetNote_ = result.notes[0];
                    ratchetVelocity_ = preAccentVelocities[0];
                }
                for (size_t i = 0; i < ratchetNoteCount_; ++i) {
                    ratchetNotes_[i] = result.notes[i];
                    ratchetVelocities_[i] = preAccentVelocities[i];
                }

                // Determine if next sub-step is the last (for look-ahead)
                ratchetIsLastSubStep_ = (ratchetSubStepsRemaining_ == 1);

                // Schedule gate noteOff for first sub-step (sub-step index 0)
                // First sub-step always schedules gate (look-ahead only on last sub-step)
                // v1.5: Use precomputed strum offsets so each note gets the same gate length
                for (size_t i = 0; i < emittedNoteOns; ++i) {
                    const size_t strumOff = static_cast<size_t>(strumOffsetFor(i));
                    scheduleGate(result.notes[i], ratchetGateDurations_[0], strumOff);
                }
            } else {
                // ratchetCount == 1: normal (Phase 5) note emission path
                ratchetSubStepsRemaining_ = 0;  // Ensure no stale sub-step state (FR-013)
                ratchetSubStepIndex_ = 0;

            if (isSlide) {
                // Slide: suppress previous noteOffs, emit legato noteOns (FR-015)
                cancelPendingNoteOffsForCurrentNotes();
                // Do NOT emit noteOffs for currently sounding notes

                if (result.count > 1) {
                    // Chord slide: emit legato noteOns for all new chord notes
                    // v1.5: Precompute strum offsets once per chord
                    prepareStrumOffsets(result.count);
                    for (size_t i = 0; i < result.count && eventCount < maxEvents; ++i) {
                        outputEvents[eventCount++] = ArpEvent{
                            .type = ArpEvent::Type::NoteOn,
                            .note = result.notes[i],
                            .velocity = result.velocities[i],
                            .sampleOffset = clampOffsetToBlock(humanizedSampleOffset + strumOffsetFor(i), blockSize),
                            .legato = true};  // legato=true
                    }
                    // Track all new chord notes as currently sounding
                    for (size_t i = 0; i < result.count && i < 32; ++i) {
                        currentArpNotes_[i] = result.notes[i];
                    }
                    currentArpNoteCount_ = result.count < 32 ? result.count : 32;
                } else {
                    // Single note slide
                    if (eventCount < maxEvents) {
                        outputEvents[eventCount++] = ArpEvent{
                            .type = ArpEvent::Type::NoteOn,
                            .note = result.notes[0],
                            .velocity = result.velocities[0],
                            .sampleOffset = humanizedSampleOffset,
                            .legato = true};  // legato=true
                    }
                    // Replace the previous note tracking with the new note
                    currentArpNotes_[0] = result.notes[0];
                    currentArpNoteCount_ = 1;
                }

                // Schedule gate-based noteOffs for the new slide notes
                // (unless next step is Tie or Slide)
                // v1.5: Use precomputed strum offsets so strummed notes get equal gate
                if (!suppressGateNoteOff) {
                    for (size_t i = 0; i < result.count; ++i) {
                        const size_t strumOff = (result.count > 1)
                            ? static_cast<size_t>(strumOffsetFor(i)) : 0;
                        scheduleGate(result.notes[i], gateDuration, strumOff);
                    }
                }
            } else if (result.count > 1) {
                // FR-022: Chord mode -- emit NoteOff for all previously
                // sounding notes first (to replace the previous chord). Drop
                // their scheduled gate NoteOffs first: with gate > 100% a
                // pending off outlives the step that scheduled it, and
                // addPendingNoteOff does not dedup, so a leftover would fire
                // mid-step and cut the note this step re-strikes. Matches the
                // rest/tie/slide paths, which all cancel before releasing.
                cancelPendingNoteOffsForCurrentNotes();
                // Release at the HUMANIZED offset, not the raw step offset.
                // Negative humanize puts humanizedSampleOffset before
                // sampleOffset, and MidiNoteDelay sorts strictly by offset, so a
                // raw-offset release would sort AFTER the new NoteOn -- for a
                // pitch held across steps (ordinary Chord mode) the host would
                // see NoteOn(p) then NoteOff(p) and kill the note it just
                // struck. Strum only pushes NoteOns forward and the release
                // carries no strum, so this offset is <= every NoteOn here, and
                // the equal case is resolved by the stable sort (the release is
                // emitted first in code order).
                size_t releasedCount = 0;
                for (; releasedCount < currentArpNoteCount_ && eventCount < maxEvents;
                     ++releasedCount) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[releasedCount], .velocity = 0, .sampleOffset = humanizedSampleOffset};
                }
                // Keep whatever the span cap stopped us from releasing.
                retainUnreleasedArpNotes(releasedCount);

                // Emit NoteOn for ALL chord notes at the same humanized offset
                // v1.5: Precompute strum offsets once per chord
                prepareStrumOffsets(result.count);
                size_t emittedNoteOns = 0;
                for (; emittedNoteOns < result.count && eventCount < maxEvents;
                     ++emittedNoteOns) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::NoteOn,
                        .note = result.notes[emittedNoteOns],
                        .velocity = result.velocities[emittedNoteOns],
                        .sampleOffset = clampOffsetToBlock(humanizedSampleOffset + strumOffsetFor(emittedNoteOns), blockSize)};
                }

                // Track the chord notes that actually sounded (FR-025). Notes
                // whose NoteOn the span cap dropped must NOT be tracked or
                // scheduled: a pending NoteOff for a note that never sounded is
                // emitted later as a phantom release to the host and the mono
                // audition voice. Appends, because the release loop above may
                // have left un-released notes in the array.
                for (size_t i = 0; i < emittedNoteOns && currentArpNoteCount_ < 32; ++i) {
                    currentArpNotes_[currentArpNoteCount_++] = result.notes[i];
                }

                // Schedule PendingNoteOff for each chord note (FR-026)
                // Skip if next step is Tie or Slide (FR-012, FR-015)
                // v1.5: Use precomputed strum offsets so strummed notes get equal gate
                if (!suppressGateNoteOff) {
                    for (size_t i = 0; i < emittedNoteOns; ++i) {
                        const size_t strumOff = static_cast<size_t>(strumOffsetFor(i));
                        scheduleGate(result.notes[i], gateDuration, strumOff);
                    }
                }
            } else {
                // Single note path (result.count == 1). Tracking and gate
                // scheduling sit INSIDE the span-cap guard: a note whose NoteOn
                // the cap dropped never sounded, so scheduling its release would
                // emit a phantom NoteOff to the host and the audition voice.
                if (eventCount < maxEvents) {
                    outputEvents[eventCount++] = ArpEvent{
                        .type = ArpEvent::Type::NoteOn,
                        .note = result.notes[0],
                        .velocity = result.velocities[0],
                        .sampleOffset = humanizedSampleOffset};

                    // Track currently sounding note (FR-025). The store must sit
                    // INSIDE the bound check -- the chord path above can leave the
                    // count at 32, and a long gate keeps those notes sounding into
                    // this step, so writing first put one element past the end of
                    // currentArpNotes_ (std::array<uint8_t, 32>).
                    if (currentArpNoteCount_ < 32) {
                        currentArpNotes_[currentArpNoteCount_++] = result.notes[0];
                    }

                    // Schedule NoteOff for this note.
                    // Skip if next step is Tie or Slide (FR-012, FR-015)
                    if (!suppressGateNoteOff) {
                        scheduleGate(result.notes[0], gateDuration, 0);
                    }
                }
            }
            } // end ratchetCount == 1 else branch
        } else {
            // result.count == 0: buffer became empty between steps (defensive).
            // Advance lanes using speed accumulators to stay synchronized
            // v1.5: Apply per-lane swing here too for consistency
            auto advanceLaneDefensive = [this](auto& lane, size_t laneIdx) {
                float& accum = laneAccumulators_[laneIdx];
                const float speed = laneSpeedMultipliers_[laneIdx];
                const float swing = laneSwingAmounts_[laneIdx];
                uint8_t& counter = laneSwingCounters_[laneIdx];

                accum += speed;
                // Swing lengthens even-numbered sub-steps and shortens odd ones.
                float threshold = 1.0f;
                if (swing > 0.0f) {
                    threshold = ((counter & 1u) == 0u) ? 1.0f + swing : 1.0f - swing;
                }

                while (accum >= threshold) {
                    accum -= threshold;
                    lane.advance();
                    ++counter;
                    if (swing > 0.0f) {
                        threshold = (counter & 1u) == 0u
                            ? 1.0f + swing
                            : 1.0f - swing;
                    }
                }
            };
            advanceLaneDefensive(modifierLane_,  3);
            advanceLaneDefensive(ratchetLane_,   4);
            advanceLaneDefensive(conditionLane_, 5);
            // 076-conditional-trigs: check condition lane wrap for loopCount_ increment (FR-037)
            if (conditionLane_.currentStep() == 0) {
                ++loopCount_;
            }
            ratchetSubStepsRemaining_ = 0;   // 074-ratcheting: clear any pending sub-steps
            ratchetSubStepIndex_ = 0;

            // 075-euclidean-timing (FR-035): advance Euclidean position in
            // defensive branch to prevent desync with other lanes
            if (euclideanEnabled_) {
                euclideanPosition_ = (euclideanPosition_ + 1)
                    % static_cast<size_t>(euclideanSteps_);
            }

            // Treat as rest -- no NoteOn emitted. Emit NoteOff for any
            // currently sounding arp note to prevent stuck notes (FR-024).
            for (size_t i = 0; i < currentArpNoteCount_ && eventCount < maxEvents; ++i) {
                outputEvents[eventCount++] = ArpEvent{
                    .type = ArpEvent::Type::NoteOff, .note = currentArpNotes_[i], .velocity = 0, .sampleOffset = sampleOffset};
            }
            currentArpNoteCount_ = 0;

            // 077-spice-dice-humanize: consume humanize PRNG in defensive branch (FR-041)
            (void)humanizeRng_.nextFloat();  // timing (discarded)
            (void)humanizeRng_.nextFloat();  // velocity (discarded)
            (void)humanizeRng_.nextFloat();  // gate (discarded)
        }

        // Increment swing step counter
        ++swingStepCounter_;

        // Recalculate step duration for next step (with swing).
        // 074-ratcheting: When ratchet sub-steps are pending (ratchetCount > 1),
        // do NOT recalculate. Keep currentStepDuration_ at the CURRENT step's
        // value so the step boundary timer fires at the correct time (end of
        // the current ratcheted step). The processBlock() Step handler
        // recalculates before calling fireStep() for the next step. (FR-025)
        if (ratchetSubStepsRemaining_ == 0) {
            currentStepDuration_ = calculateStepDuration(ctx);
        }
    }

}  // namespace Krate::DSP
