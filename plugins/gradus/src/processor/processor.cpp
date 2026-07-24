// ==============================================================================
// Gradus Processor Implementation
// ==============================================================================

#include "processor.h"
#include "../plugin_ids.h"

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/note_value.h>

#include "midi/midi_event_dispatcher.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>


using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Krate::DSP;

namespace Gradus {

Processor::Processor()
{
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    // Event input: MIDI notes in
    addEventInput(STR16("Event Input"));

    // Event output: arpeggiated MIDI notes out
    addEventOutput(STR16("MIDI Output"));

    // Audio output: stereo (for audition sound)
    addAudioOutput(STR16("Audio Output"), SpeakerArr::kStereo);

    return kResultOk;
}

tresult PLUGIN_API Processor::terminate()
{
    return AudioEffect::terminate();
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state) {
        // Skip the reset when a deactivation flush is still outstanding: it
        // would clear the very note-off obligations recorded below, which the
        // first process() after reactivation still has to emit.
        if (pendingDeactivateFlush_) {
            pendingDeactivateFlush_ = false;
        } else {
            arpCore_.reset();  // Also resets midiDelayLane_ inside
            midiDelay_.reset();
        }
    } else {
        // Gradus emits its own arp and echo NoteOns to a MIDI *output* bus that
        // the host cannot recall, so deactivating mid-note would strand them
        // downstream. VST3 does not call process() after setActive(false)
        // returns, so we cannot emit here -- instead record the obligation and
        // let the first process() after reactivation discharge it.
        arpCore_.requestPanicNoteOff();
        midiDelay_.flushWithNoteOffs();
        pendingDeactivateFlush_ = true;
        auditionVoice_.reset();
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;

    // A host reconfigures with setActive(false) -> setupProcessing() ->
    // setActive(true). The plain prepare() resets the arp, which would wipe both
    // the panic obligation recorded on deactivation and the sounding-note list
    // it has to release -- while midiDelay_ (untouched here) still flushes its
    // own echoes, leaving the two paths asymmetric and the arp notes stranded.
    if (pendingDeactivateFlush_)
        arpCore_.prepareRetainingPanicNoteOff(sampleRate_, static_cast<size_t>(maxBlockSize_));
    else
        arpCore_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_));
    auditionVoice_.prepare(sampleRate_);

    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    // No audio inputs, one stereo output
    if (numIns != 0 || numOuts != 1)
        return kResultFalse;

    if (outputs[0] != SpeakerArr::kStereo)
        return kResultFalse;

    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    // --- 1. Process parameter changes ---
    if (data.inputParameterChanges)
        processParameterChanges(data.inputParameterChanges);

    // --- 2. Process incoming MIDI events → feed to arpCore ---
    if (data.inputEvents) {
        int32 eventCount = data.inputEvents->getEventCount();
        for (int32 i = 0; i < eventCount; ++i) {
            Event e{};
            if (data.inputEvents->getEvent(i, e) == kResultOk) {
                if (e.type == Event::kNoteOnEvent) { // NOLINT(bugprone-branch-clone)
                    // MIDI 1.0 running status expresses a key release as a
                    // NoteOn with velocity 0, and hosts forward that verbatim on
                    // this legacy Event path. Treating it as a note-on would
                    // hold the pitch forever (the sender never follows up with a
                    // kNoteOffEvent) and permanently over-count
                    // physicalKeysHeld_. Note this convention is MIDI-1.0 only:
                    // a MIDI-2.0 UMP velocity-0 note-on is a genuine note-on.
                    if (!std::isfinite(e.noteOn.velocity) || e.noteOn.velocity <= 0.0f) {
                        arpCore_.noteOff(static_cast<uint8_t>(e.noteOn.pitch));
                    } else {
                        // Round, do not truncate. The output path divides by
                        // 127, so truncating here breaks the round trip: 72/127
                        // is just under 0.566929 in float32, and multiplying
                        // back gives 71.9999 -> 71. Eight of the 127 MIDI
                        // velocities lost an LSB that way. Clamping also keeps
                        // the float->uint8_t conversion defined if a
                        // non-conformant host sends velocity outside [0,1]
                        // (VST3 specifies normalized velocity, so this is
                        // hardening, not a live-path change).
                        const long midiVelocity = std::lround(
                            static_cast<double>(e.noteOn.velocity) * 127.0);
                        arpCore_.noteOn(
                            static_cast<uint8_t>(e.noteOn.pitch),
                            static_cast<uint8_t>(std::clamp(midiVelocity, 1L, 127L)));
                    }
                } else if (e.type == Event::kNoteOffEvent) {
                    arpCore_.noteOff(
                        static_cast<uint8_t>(e.noteOff.pitch));
                }
            }
        }
    }

    // --- 3. Apply params to arp engine ---
    applyParamsToEngine();
    arpCore_.consumePendingCurveTables();

    // --- 4. Transport sync ---
    // Source = Sequencer behaves like a step sequencer and follows the host
    // transport (spec 142 US1/FR-031): Stop must halt note emission. Source =
    // Live is a classic arp that free-runs off held notes, so it must keep
    // clocking even when the transport is stopped.
    const bool isSequencer =
        arpParams_.sourceMode.load(std::memory_order_relaxed) == 1;
    if (data.processContext) {
        const auto& ctx = *data.processContext;
        const bool isPlaying = (ctx.state & ProcessContext::kPlaying) != 0;
        const bool hasMusicalPos = (ctx.state & ProcessContext::kProjectTimeMusicValid) != 0;

        // Detect transport start/loop
        if (hasMusicalPos) {
            if (isPlaying && ctx.projectTimeMusic < prevProjectTimeMusic_) {
                arpCore_.notifyTransportLoop();
            }
            if (isPlaying) {
                arpCore_.syncToMusicalPosition(ctx.projectTimeMusic);
            }
            prevProjectTimeMusic_ = ctx.projectTimeMusic;
        }

        // Restart the pattern on the rising transport-play edge -- but ONLY in
        // Sequencer mode. The sequencer is transport-gated, so it is silent
        // while stopped and a reset here is a clean pattern restart.
        //
        // Live mode free-runs off held notes: at this instant notes are already
        // sounding and a chord is already held. reset() emits no NoteOff (it
        // just clears currentArpNotes_/pendingNoteOffs_) and clears heldNotes_,
        // so resetting here orphaned every sounding note downstream and left
        // the arp silent until the keys were physically re-pressed. Pressing
        // Play is simply not the free-running arp's business.
        //
        // The echo tail is the host's problem either way: midiDelay_.process()
        // runs every block regardless of transport (only step generation is
        // gated at BlockContext::isPlaying), so echo NoteOns keep going out
        // while stopped with their NoteOffs still pending. reset() would discard
        // those obligations, so flush them the way setActive(false) does -- the
        // retained NoteOffs discharge in this same block's midiDelay_.process().
        if (isSequencer && isPlaying && !wasTransportPlaying_) {
            arpCore_.reset();  // Also resets midiDelayLane_ inside
            midiDelay_.flushWithNoteOffs();
        }
        wasTransportPlaying_ = isPlaying;
    }

    // --- 5. Process arp block ---
    BlockContext blockCtx{};
    blockCtx.sampleRate = sampleRate_;
    blockCtx.blockSize = static_cast<size_t>(data.numSamples);
    if (data.processContext) {
        const bool transportPlaying =
            (data.processContext->state & ProcessContext::kPlaying) != 0;
        // Fall back to 120 BPM for any non-positive tempo, whatever the
        // transport state. The fallback used to sit inside the !playing branch
        // only, so a host reporting kPlaying with tempo 0 left tempoBPM at 0 --
        // and synced step math divides 60.0 / tempoBPM.
        blockCtx.tempoBPM = data.processContext->tempo > 0
            ? data.processContext->tempo : 120.0;
        // Sequencer follows the host transport; Live free-runs.
        blockCtx.isPlaying = isSequencer ? transportPlaying : true;
    } else {
        // No host transport context (e.g. offline/standalone): Sequencer has no
        // transport to follow, so keep it running; Live free-runs as always.
        blockCtx.tempoBPM = 120.0;
        blockCtx.isPlaying = true;
    }

    size_t numArpEvents = arpCore_.processBlock(
        blockCtx, std::span<ArpEvent>(arpEvents_.data(), arpEvents_.size()));

    // --- 5b. MIDI delay post-processing ---
    // The delay lane is now inside arpCore_ and advances with the other 8 lanes.
    size_t currentDelayStep = arpCore_.midiDelayLane().currentStep();
    size_t numCombinedEvents = midiDelay_.process(
        blockCtx,
        std::span<const ArpEvent>(arpEvents_.data(), numArpEvents),
        numArpEvents,
        std::span<ArpEvent>(combinedEvents_.data(), combinedEvents_.size()),
        currentDelayStep);

    // --- 6. Route combined events → MIDI output ---
    if (data.outputEvents) {
        for (size_t i = 0; i < numCombinedEvents; ++i) {
            const auto& evt = combinedEvents_[i];

            Event outEvent{};
            outEvent.busIndex = 0;
            outEvent.sampleOffset = evt.sampleOffset;
            outEvent.ppqPosition = 0;
            outEvent.flags = 0;

            if (evt.type == ArpEvent::Type::NoteOn) {
                outEvent.type = Event::kNoteOnEvent;
                outEvent.noteOn.channel = 0;
                outEvent.noteOn.pitch = static_cast<int16>(evt.note);
                outEvent.noteOn.velocity = static_cast<float>(evt.velocity) / 127.0f;
                outEvent.noteOn.noteId = -1;  // Gradus doesn't track note IDs
                outEvent.noteOn.tuning = 0.0f;
                outEvent.noteOn.length = 0;
                data.outputEvents->addEvent(outEvent);

                // Feed audition voice
                if (auditionEnabled_.load(std::memory_order_relaxed)) {
                    auditionVoice_.noteOn(evt.note, evt.velocity);
                }
            } else if (evt.type == ArpEvent::Type::NoteOff) {
                outEvent.type = Event::kNoteOffEvent;
                outEvent.noteOff.channel = 0;
                outEvent.noteOff.pitch = static_cast<int16>(evt.note);
                outEvent.noteOff.velocity = 0.0f;
                outEvent.noteOff.noteId = -1;  // Gradus doesn't track note IDs
                outEvent.noteOff.tuning = 0.0f;
                data.outputEvents->addEvent(outEvent);

                // Feed audition voice
                auditionVoice_.noteOff();
            }
        }
    }

    // --- 7. Output playhead parameters ---
    if (data.outputParameterChanges) {
        auto outputPlayhead = [&](ParamID id, float value) {
            int32 index = 0;
            auto* queue = data.outputParameterChanges->addParameterData(id, index);
            if (queue) {
                int32 sampleOffset = 0;
                queue->addPoint(sampleOffset, static_cast<ParamValue>(value), index);
            }
        };

        const std::pair<ParamID, size_t> playheads[] = {
            {kArpVelocityPlayheadId,  arpCore_.velocityLane().currentStep()},
            {kArpGatePlayheadId,      arpCore_.gateLane().currentStep()},
            {kArpPitchPlayheadId,     arpCore_.pitchLane().currentStep()},
            {kArpRatchetPlayheadId,   arpCore_.ratchetLane().currentStep()},
            {kArpModifierPlayheadId,  arpCore_.modifierLane().currentStep()},
            {kArpConditionPlayheadId, arpCore_.conditionLane().currentStep()},
            {kArpChordPlayheadId,     arpCore_.chordLane().currentStep()},
            {kArpInversionPlayheadId, arpCore_.inversionLane().currentStep()},
            {kArpMidiDelayPlayheadId, arpCore_.midiDelayLane().currentStep()},
            // Spec 142: Sequencer Note lane (lane 9) playhead for UI piano-roll cursor.
            {kArpSequencerNoteLanePlayheadId, arpCore_.seqNoteLane().currentStep()},
        };
        for (const auto& [id, step] : playheads) {
            outputPlayhead(id, static_cast<float>(step) / kMaxLaneStepsF);
        }
    }

    // --- 8. Audio output: audition voice ---
    if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2) {
        auto numSamples = static_cast<size_t>(data.numSamples);
        float* outL = data.outputs[0].channelBuffers32[0];
        float* outR = data.outputs[0].channelBuffers32[1];

        // Clear buffers
        for (size_t s = 0; s < numSamples; ++s) {
            outL[s] = 0.0f;
            outR[s] = 0.0f;
        }

        // Apply audition voice params and render
        const bool auditionOn = auditionEnabled_.load(std::memory_order_relaxed);
        if (auditionOn) {
            auditionVoice_.setWaveform(auditionWaveform_.load(std::memory_order_relaxed));
            auditionVoice_.setDecay(auditionDecay_.load(std::memory_order_relaxed));
            auditionVoice_.setVolume(auditionVolume_.load(std::memory_order_relaxed));
            auditionVoice_.processBlock(outL, outR, numSamples);
        } else if (auditionVoice_.isActive()) {
            // The voice stops being rendered the moment audition is switched
            // off, and AuditionVoice::active_ only clears from inside
            // processBlock -- so without this it would stay flagged active
            // forever, and the silence report below would keep claiming a
            // buffer we are in fact leaving at zero.
            auditionVoice_.reset();
        }

        // Report silence from what we actually wrote: the buffers were cleared
        // above and only the enabled audition voice adds to them.
        data.outputs[0].silenceFlags =
            (auditionOn && auditionVoice_.isActive()) ? 0 : 0x3;
    }

    return kResultOk;
}

// ==============================================================================
// State Save/Load
// ==============================================================================

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    // 1. State version (bumped to 3 in spec 142 for Sequencer Note lane)
    streamer.writeInt32(kCurrentStateVersion);

    // 2. Arp params (audition params are session-only, not saved in presets)
    saveArpParams(arpParams_, streamer);

    // 3. Sequencer Note lane appendix (spec 142, v3)
    saveSequencerNoteLaneParams(arpParams_, streamer);

    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    // 1. Read state version and dispatch.
    //    v2: legacy stream (no Sequencer Note lane appendix); EOF on the new
    //        block leaves defaults in place (FR-039a).
    //    v3: full v2 block + Sequencer Note lane appendix.
    //   >v3: future-incompatible; reject defensively.
    int32 version = 0;
    if (!streamer.readInt32(version))
        return kResultFalse;
    if (version != 2 && version != 3)
        return kResultFalse;

    // 2. Load arp params (v2 block; identical layout in v2 and v3 streams).
    if (!loadArpParams(arpParams_, streamer))
        return kResultFalse;

    // 3. Load Sequencer Note lane appendix.
    //    For v2 streams this hits EOF on the first field and leaves defaults
    //    intact. For v3 streams it reads the appendix fully; a corrupt stream
    //    returns false here.
    if (!loadSequencerNoteLaneParams(arpParams_, streamer))
        return kResultFalse;

    // Force arp to MIDI mode (always on in Gradus)
    arpParams_.operatingMode.store(kArpMIDI, std::memory_order_relaxed);

    // Bake speed curve tables from deserialized curve data and apply to arpCore
    {
        std::scoped_lock lock(arpParams_.speedCurveMutex_);
        for (size_t i = 0; i < 8; ++i) {
            const auto& curve = arpParams_.speedCurves[i];
            arpParams_.speedCurveEnabledFlags[i].store(
                curve.enabled, std::memory_order_relaxed);
            std::array<float, 256> table{};
            curve.bakeToTable(table);
            arpCore_.setLaneSpeedCurveTable(i, table);
            arpCore_.setLaneSpeedCurveEnabled(i, curve.enabled);
        }
    }

    // Audition params are session-only — not loaded from presets

    return kResultOk;
}

// ==============================================================================
// IMessage Handler — receives speed curve tables from controller
// ==============================================================================

tresult PLUGIN_API Processor::notify(IMessage* message)
{
    if (!message) return kResultFalse;

    // Spec 142: Sequencer Note lane param update from Controller. This is the
    // belt-and-suspenders path that guarantees the Processor's atomics track
    // UI edits even if the host doesn't relay them through processParameter
    // Changes. See controller.h::sendSeqNoteLaneParam for the rationale.
    if (strcmp(message->getMessageID(), "SeqNoteLaneParam") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs) return kResultFalse;

        int64 idRaw = 0;
        int64 valueBits = 0;
        if (attrs->getInt("id", idRaw) != kResultOk) return kResultFalse;
        if (attrs->getInt("valueBits", valueBits) != kResultOk) return kResultFalse;

        Steinberg::Vst::ParamValue value = 0.0;
        std::memcpy(&value, &valueBits, sizeof(Steinberg::Vst::ParamValue));

        auto id = static_cast<Steinberg::Vst::ParamID>(idRaw);
        // Reuse the same handler used by processParameterChanges so the
        // atomic-update logic stays in one place.
        Gradus::handleArpParamChange(arpParams_, id, value);
        return kResultOk;
    }

    if (strcmp(message->getMessageID(), "SpeedCurveTable") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs) return kResultFalse;

        int64 lane = 0;
        int64 enabled = 0;
        if (attrs->getInt("lane", lane) != kResultOk) return kResultFalse;
        if (lane < 0 || lane >= 8) return kResultFalse;
        attrs->getInt("enabled", enabled);

        auto laneIdx = static_cast<size_t>(lane);
        arpParams_.speedCurveEnabledFlags[laneIdx].store(
            enabled != 0, std::memory_order_relaxed);

        // Read depth value sent directly from controller
        int64 depthBits = 0;
        if (attrs->getInt("depth", depthBits) == kResultOk) {
            float depthVal = 0.0f;
            std::memcpy(&depthVal, &depthBits, sizeof(float));
            arpCore_.setLaneSpeedCurveDepth(laneIdx, depthVal);
        }

        // Read baked table from binary attribute (256 floats = 1024 bytes)
        const void* data = nullptr;
        Steinberg::uint32 size = 0;
        if (attrs->getBinary("table", data, size) == kResultOk &&
            data && size == 256 * sizeof(float)) {
            std::array<float, 256> table{};
            std::memcpy(table.data(), data, size);
            arpCore_.setLaneSpeedCurveTable(laneIdx, table);
        }

        // Read curve point data for serialization (JSON-like binary blob)
        const void* curveData = nullptr;
        Steinberg::uint32 curveSize = 0;
        if (attrs->getBinary("curveData", curveData, curveSize) == kResultOk &&
            curveData && curveSize > 0) {
            std::scoped_lock lock(arpParams_.speedCurveMutex_);
            auto& curve = arpParams_.speedCurves[laneIdx];
            curve.enabled = (enabled != 0);

            // Deserialize: presetIndex(int32) + numPoints(int32) + points(6 floats each)
            const auto* bytes = static_cast<const char*>(curveData);
            Steinberg::uint32 offset = 0;
            auto readInt = [&](int32& val) -> bool {
                if (offset + sizeof(int32) > curveSize) return false;
                std::memcpy(&val, bytes + offset, sizeof(int32));
                offset += sizeof(int32);
                return true;
            };
            auto readFloat = [&](float& val) -> bool {
                if (offset + sizeof(float) > curveSize) return false;
                std::memcpy(&val, bytes + offset, sizeof(float));
                offset += sizeof(float);
                return true;
            };

            int32 presetIdx = 0;
            int32 numPoints = 0;
            if (readInt(presetIdx) && readInt(numPoints)) {
                curve.presetIndex = presetIdx;
                numPoints = std::clamp(numPoints, int32{0}, int32{64});
                curve.points.clear();
                curve.points.reserve(static_cast<size_t>(numPoints));
                for (int32 i = 0; i < numPoints; ++i) {
                    SpeedCurvePoint pt;
                    if (!readFloat(pt.x) || !readFloat(pt.y) ||
                        !readFloat(pt.cpLeftX) || !readFloat(pt.cpLeftY) ||
                        !readFloat(pt.cpRightX) || !readFloat(pt.cpRightY)) break;
                    curve.points.push_back(pt);
                }
            }
        }

        return kResultOk;
    }

    return AudioEffect::notify(message);
}

// ==============================================================================
// Parameter Changes
// ==============================================================================

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Processor::processParameterChanges(IParameterChanges* changes)
{
    int32 numParams = changes->getParameterCount();
    for (int32 i = 0; i < numParams; ++i) {
        auto* queue = changes->getParameterData(i);
        if (!queue) continue;

        ParamID id = queue->getParameterId();
        int32 numPoints = queue->getPointCount();
        if (numPoints <= 0) continue;

        ParamValue value = 0.0;
        int32 sampleOffset = 0;

        // Use the last point value
        if (queue->getPoint(numPoints - 1, sampleOffset, value) != kResultOk)
            continue;

        // Try arp params (base range 3000-3495 + speed curve 3500-3507 + delay 3510-3740
        // + sequencer note lane 3741-3811, spec 142)
        if ((id >= kArpBaseId && id <= kArpEndId) ||
            (id >= kArpVelocityLaneSpeedCurveDepthId && id <= kArpInversionLaneSpeedCurveDepthId) ||
            (id >= kArpMidiDelayLaneLengthId && id <= kArpMidiDelayPlayheadId) ||
            (id >= kArpSourceModeId && id <= kArpSequencerNoteLaneEndId)) {
            handleArpParamChange(arpParams_, id, value);
            continue;
        }

        // Audition params
        if (id == kAuditionEnabledId) { // NOLINT(bugprone-branch-clone)
            auditionEnabled_.store(value >= 0.5, std::memory_order_relaxed);
        } else if (id == kAuditionVolumeId) {
            auditionVolume_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (id == kAuditionWaveformId) {
            int wf = static_cast<int>(std::round(value * (kAuditionWaveformCount - 1)));
            auditionWaveform_.store(std::clamp(wf, 0, kAuditionWaveformCount - 1),
                std::memory_order_relaxed);
        } else if (id == kAuditionDecayId) {
            float decay = kAuditionDecayMinMs + static_cast<float>(value) * kAuditionDecayRangeMs;
            auditionDecay_.store(decay, std::memory_order_relaxed);
        }
    }
}

// ==============================================================================
// Apply Params to Engine (simplified from Ruinae — no mod offsets)
// ==============================================================================

void Processor::applyParamsToEngine()
{
    using namespace Krate::DSP;

    // Mode — only call when value changes (resets step index)
    {
        const auto modeInt = arpParams_.mode.load(std::memory_order_relaxed);
        const auto mode = static_cast<ArpMode>(modeInt);
        if (mode != prevArpMode_) {
            arpCore_.setMode(mode);
            prevArpMode_ = mode;
        }
    }
    {
        const auto octaveMode = static_cast<OctaveMode>(
            arpParams_.octaveMode.load(std::memory_order_relaxed));
        if (octaveMode != prevArpOctaveMode_) {
            arpCore_.setOctaveMode(octaveMode);
            prevArpOctaveMode_ = octaveMode;
        }
    }
    {
        const auto noteValue = arpParams_.noteValue.load(std::memory_order_relaxed);
        if (noteValue != prevArpNoteValue_) {
            auto mapping = getNoteValueFromDropdown(noteValue);
            arpCore_.setNoteValue(mapping.note, mapping.modifier);
            prevArpNoteValue_ = noteValue;
        }
    }

    // Direct parameter application (no modulation)
    arpCore_.setTempoSync(arpParams_.tempoSync.load(std::memory_order_relaxed));
    arpCore_.setFreeRate(arpParams_.freeRate.load(std::memory_order_relaxed));
    arpCore_.setGateLength(arpParams_.gateLength.load(std::memory_order_relaxed));
    {
        const auto octaveRange = arpParams_.octaveRange.load(std::memory_order_relaxed);
        if (octaveRange != prevArpOctaveRange_) {
            arpCore_.setOctaveRange(octaveRange);
            prevArpOctaveRange_ = octaveRange;
        }
    }
    arpCore_.setSwing(arpParams_.swing.load(std::memory_order_relaxed));
    arpCore_.setSpice(arpParams_.spice.load(std::memory_order_relaxed));

    {
        const auto latchMode = static_cast<LatchMode>(
            arpParams_.latchMode.load(std::memory_order_relaxed));
        if (latchMode != prevArpLatchMode_) {
            arpCore_.setLatchMode(latchMode);
            prevArpLatchMode_ = latchMode;
        }
    }
    {
        const auto retrigger = static_cast<ArpRetriggerMode>(
            arpParams_.retrigger.load(std::memory_order_relaxed));
        if (retrigger != prevArpRetrigger_) {
            arpCore_.setRetrigger(retrigger);
            prevArpRetrigger_ = retrigger;
        }
    }

    // --- Sync float-step lanes (Velocity, Gate) ---
    auto syncFloatLane = [&](auto& lane, const std::atomic<float>* steps,
                             const std::atomic<int>& lengthParam) {
        const auto len = lengthParam.load(std::memory_order_relaxed);
        lane.setLength(kMaxLaneSteps);
        for (int i = 0; i < kMaxLaneSteps; ++i) {
            lane.setStep(static_cast<size_t>(i),
                steps[i].load(std::memory_order_relaxed));
        }
        lane.setLength(static_cast<size_t>(len));
    };
    syncFloatLane(arpCore_.velocityLane(), arpParams_.velocityLaneSteps.data(),
        arpParams_.velocityLaneLength);
    syncFloatLane(arpCore_.gateLane(), arpParams_.gateLaneSteps.data(),
        arpParams_.gateLaneLength);

    // --- Sync int-step lanes (Pitch, Modifier, Ratchet, Condition, Chord, Inversion) ---
    auto syncIntLane = [&](auto& lane, const auto* steps,
                           const std::atomic<int>& lengthParam,
                           auto minVal, auto maxVal) {
        const auto len = lengthParam.load(std::memory_order_relaxed);
        lane.setLength(kMaxLaneSteps);
        for (int i = 0; i < kMaxLaneSteps; ++i) {
            auto val = std::clamp(steps[i].load(std::memory_order_relaxed),
                static_cast<decltype(steps[0].load())>(minVal),
                static_cast<decltype(steps[0].load())>(maxVal));
            lane.setStep(static_cast<size_t>(i),
                static_cast<decltype(lane.getStep(0))>(val));
        }
        lane.setLength(static_cast<size_t>(len));
    };
    syncIntLane(arpCore_.pitchLane(), arpParams_.pitchLaneSteps.data(),
        arpParams_.pitchLaneLength, -24, 24);
    // Modifier lane: uint8_t steps, no clamping needed (full range)
    {
        const auto modLen = arpParams_.modifierLaneLength.load(std::memory_order_relaxed);
        arpCore_.modifierLane().setLength(kMaxLaneSteps);
        for (int i = 0; i < kMaxLaneSteps; ++i) {
            arpCore_.modifierLane().setStep(static_cast<size_t>(i),
                static_cast<uint8_t>(arpParams_.modifierLaneSteps[i].load(
                    std::memory_order_relaxed)));
        }
        arpCore_.modifierLane().setLength(static_cast<size_t>(modLen));
    }
    arpCore_.setAccentVelocity(arpParams_.accentVelocity.load(std::memory_order_relaxed));
    arpCore_.setSlideTime(arpParams_.slideTime.load(std::memory_order_relaxed));

    syncIntLane(arpCore_.ratchetLane(), arpParams_.ratchetLaneSteps.data(),
        arpParams_.ratchetLaneLength, 1, 4);

    // --- Euclidean Timing ---
    arpCore_.setEuclideanSteps(
        arpParams_.euclideanSteps.load(std::memory_order_relaxed));
    arpCore_.setEuclideanHits(
        arpParams_.euclideanHits.load(std::memory_order_relaxed));
    arpCore_.setEuclideanRotation(
        arpParams_.euclideanRotation.load(std::memory_order_relaxed));
    arpCore_.setEuclideanEnabled(
        arpParams_.euclideanEnabled.load(std::memory_order_relaxed));

    syncIntLane(arpCore_.conditionLane(), arpParams_.conditionLaneSteps.data(),
        arpParams_.conditionLaneLength, 0, 17);
    arpCore_.setFillActive(arpParams_.fillToggle.load(std::memory_order_relaxed));

    // --- Dice & Humanize ---
    {
        bool expected = true;
        if (arpParams_.diceTrigger.compare_exchange_strong(
                expected, false, std::memory_order_relaxed)) {
            arpCore_.triggerDice();
        }
    }
    arpCore_.setHumanize(arpParams_.humanize.load(std::memory_order_relaxed));

    // --- Ratchet Swing ---
    arpCore_.setRatchetSwing(arpParams_.ratchetSwing.load(std::memory_order_relaxed));

    // --- Scale Mode ---
    arpCore_.setScaleType(static_cast<Krate::DSP::ScaleType>(
        arpParams_.scaleType.load(std::memory_order_relaxed)));
    arpCore_.setRootNote(arpParams_.rootNote.load(std::memory_order_relaxed));
    arpCore_.setScaleQuantizeInput(
        arpParams_.scaleQuantizeInput.load(std::memory_order_relaxed));

    // --- Markov Matrix (49 cells) ---
    // Copy atomic snapshots into a plain array for the NoteSelector.
    {
        std::array<float, Krate::DSP::kMarkovMatrixSize> matrix{};
        for (size_t i = 0; i < Krate::DSP::kMarkovMatrixSize; ++i) {
            matrix[i] =
                arpParams_.markovMatrix[i].load(std::memory_order_relaxed);
        }
        arpCore_.setMarkovMatrix(matrix);
    }

    syncIntLane(arpCore_.chordLane(), arpParams_.chordLaneSteps.data(),
        arpParams_.chordLaneLength, 0, 4);
    syncIntLane(arpCore_.inversionLane(), arpParams_.inversionLaneSteps.data(),
        arpParams_.inversionLaneLength, 0, 3);
    arpCore_.setVoicingMode(static_cast<Krate::DSP::VoicingMode>(
        arpParams_.voicingMode.load(std::memory_order_relaxed)));

    // Per-lane speed multipliers
    arpCore_.setLaneSpeed(0, arpParams_.velocityLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(1, arpParams_.gateLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(2, arpParams_.pitchLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(3, arpParams_.modifierLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(4, arpParams_.ratchetLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(5, arpParams_.conditionLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(6, arpParams_.chordLaneSpeed.load(std::memory_order_relaxed));
    arpCore_.setLaneSpeed(7, arpParams_.inversionLaneSpeed.load(std::memory_order_relaxed));

    // Per-lane speed curve depth: read from IMessage-set values stored
    // directly on arpCore_ (arpParams_ atomics are unreliable for
    // programmatically-created knobs — the host may not relay performEdit
    // back through processParameterChanges). Depth is set in notify().

    // Per-lane speed curve: apply enabled flags. Consume any pending table
    // updates from IMessage (staging → active).
    for (size_t i = 0; i < 8; ++i) {
        arpCore_.setLaneSpeedCurveEnabled(i,
            arpParams_.speedCurveEnabledFlags[i].load(std::memory_order_relaxed));
    }


    // v1.5 Features
    arpCore_.setRatchetDecay(arpParams_.ratchetDecay.load(std::memory_order_relaxed));
    arpCore_.setStrumTime(arpParams_.strumTime.load(std::memory_order_relaxed));
    arpCore_.setStrumDirection(arpParams_.strumDirection.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(0, arpParams_.velocityLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(1, arpParams_.gateLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(2, arpParams_.pitchLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(3, arpParams_.modifierLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(4, arpParams_.ratchetLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(5, arpParams_.conditionLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(6, arpParams_.chordLaneSwing.load(std::memory_order_relaxed));
    arpCore_.setLaneSwing(7, arpParams_.inversionLaneSwing.load(std::memory_order_relaxed));

    // v1.5 Part 2 Features
    arpCore_.setVelocityCurveType(arpParams_.velocityCurveType.load(std::memory_order_relaxed));
    arpCore_.setVelocityCurveAmount(arpParams_.velocityCurveAmount.load(std::memory_order_relaxed));
    arpCore_.setTranspose(arpParams_.transpose.load(std::memory_order_relaxed));
    arpCore_.setLaneLengthJitter(0, arpParams_.velocityLaneJitter.load(std::memory_order_relaxed));
    arpCore_.setLaneLengthJitter(1, arpParams_.gateLaneJitter.load(std::memory_order_relaxed));
    arpCore_.setLaneLengthJitter(2, arpParams_.pitchLaneJitter.load(std::memory_order_relaxed));
    arpCore_.setLaneLengthJitter(3, arpParams_.modifierLaneJitter.load(std::memory_order_relaxed));
    arpCore_.setLaneLengthJitter(4, arpParams_.ratchetLaneJitter.load(std::memory_order_relaxed));
    arpCore_.setLaneLengthJitter(5, arpParams_.conditionLaneJitter.load(std::memory_order_relaxed));
    arpCore_.setLaneLengthJitter(6, arpParams_.chordLaneJitter.load(std::memory_order_relaxed));
    arpCore_.setLaneLengthJitter(7, arpParams_.inversionLaneJitter.load(std::memory_order_relaxed));

    // v1.5 Part 3: Note Range Mapping
    arpCore_.setRangeLow(arpParams_.rangeLow.load(std::memory_order_relaxed));
    arpCore_.setRangeHigh(arpParams_.rangeHigh.load(std::memory_order_relaxed));
    arpCore_.setRangeMode(arpParams_.rangeMode.load(std::memory_order_relaxed));

    // v1.5 Part 3: Step Pinning
    arpCore_.setPinNote(arpParams_.pinNote.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        arpCore_.setStepPinned(static_cast<size_t>(i),
            arpParams_.pinFlags[i].load(std::memory_order_relaxed) != 0);
    }

    // Always enabled in Gradus (no operating mode selector)
    arpCore_.setEnabled(true);

    // --- MIDI Delay Lane ---
    {
        using namespace Krate::DSP;
        const auto delayLen = arpParams_.midiDelayLaneLength.load(std::memory_order_relaxed);
        arpCore_.midiDelayLane().setLength(static_cast<size_t>(delayLen));

        // Lane 8 modulators (speed / swing / jitter / speed-curve depth).
        // The delay lane advances via advanceLaneBySpeed(midiDelayLane_, 8),
        // which reads exactly these slots -- without these calls params
        // 3703-3706 were stored, saved, loaded and shown in the UI but never
        // reached the engine, so the lane always clocked at the base rate.
        // Mirrors the lane-9 (Sequencer Note) block below.
        arpCore_.setLaneSpeed(8,
            arpParams_.midiDelayLaneSpeed.load(std::memory_order_relaxed));
        arpCore_.setLaneSwing(8,
            arpParams_.midiDelayLaneSwing.load(std::memory_order_relaxed));
        arpCore_.setLaneLengthJitter(8,
            arpParams_.midiDelayLaneJitter.load(std::memory_order_relaxed));
        // Depth only takes effect once lane 8's speed curve is enabled; the
        // enabled flags currently cover lanes 0-7 only. Applied anyway so the
        // engine matches the stored value if that changes.
        arpCore_.setLaneSpeedCurveDepth(8,
            arpParams_.midiDelayLaneSpeedCurveDepth.load(std::memory_order_relaxed));

        for (int i = 0; i < 32; ++i) {
            MidiDelayStepConfig cfg;
            cfg.timeMode = arpParams_.midiDelayTimeModeSteps[i].load(std::memory_order_relaxed)
                           ? TimeMode::Synced : TimeMode::Free;

            float rawTime = arpParams_.midiDelayTimeSteps[i].load(std::memory_order_relaxed);
            if (cfg.timeMode == TimeMode::Free) {
                // Normalized 0-1 → 10-2000ms
                cfg.delayTimeMs = kAuditionDecayMinMs + rawTime * kAuditionDecayRangeMs;
            } else {
                // Normalized 0-1 → note value index 0-29
                cfg.noteValueIndex = std::clamp(
                    static_cast<int>(std::round(rawTime * 29.0f)), 0, 29);
            }

            cfg.active = arpParams_.midiDelayActiveSteps[i].load(std::memory_order_relaxed) != 0;
            cfg.feedbackCount = arpParams_.midiDelayFeedbackSteps[i].load(std::memory_order_relaxed);
            cfg.velocityDecay = arpParams_.midiDelayVelDecaySteps[i].load(std::memory_order_relaxed);
            cfg.pitchShiftPerRepeat = arpParams_.midiDelayPitchShiftSteps[i].load(std::memory_order_relaxed);
            cfg.gateScaling = arpParams_.midiDelayGateScaleSteps[i].load(std::memory_order_relaxed);

            midiDelay_.setStepConfig(static_cast<size_t>(i), cfg);
        }
    }

    // --- Spec 142: Sequencer Note lane (lane 9) sync ---
    {
        // Sync source mode (Live / Sequencer). On a Live->Seq or Seq->Live
        // transition, emit a panic note-off for any sounding programmed note
        // BEFORE handing control to the new source. Lane playheads are NOT
        // reset (spec Q5-A). Block-boundary granularity is acceptable —
        // Source toggle is a meta-control, not sample-accurate.
        const int newSourceMode = std::clamp(
            arpParams_.sourceMode.load(std::memory_order_relaxed), 0, 1);
        if (newSourceMode != prevSourceMode_) {
            // Panic note-off for any sounding programmed note BEFORE handing
            // the pipeline to the new source. requestPanicNoteOff() sets the
            // arp's needsDisableNoteOff_ flag; the next processBlock() emits
            // a noteOff at sample 0 for every entry in currentArpNotes_,
            // then clears the buffer. Lane playheads are NOT touched (Q5-A).
            arpCore_.requestPanicNoteOff();
            arpCore_.setSourceMode(static_cast<Krate::DSP::SourceMode>(newSourceMode));
            prevSourceMode_ = newSourceMode;
        } else {
            // Idempotent: always assert the current source mode so a stale
            // arpCore_ (e.g. after setState) gets the right value.
            arpCore_.setSourceMode(static_cast<Krate::DSP::SourceMode>(newSourceMode));
        }

        // Sync lane 9 (Sequencer Note) pattern: length, 32 pitches, 32 rest flags.
        const auto seqLen = std::clamp(
            arpParams_.seqNoteLaneLength.load(std::memory_order_relaxed), 1, 32);
        arpCore_.seqNoteLane().setLength(kMaxLaneSteps);
        for (int i = 0; i < kMaxLaneSteps; ++i) {
            const int pitch = std::clamp(
                arpParams_.seqNoteLanePitches[i].load(std::memory_order_relaxed),
                0, 127);
            arpCore_.seqNoteLane().setStep(static_cast<size_t>(i),
                static_cast<uint8_t>(pitch));
        }
        arpCore_.seqNoteLane().setLength(static_cast<size_t>(seqLen));
        for (int i = 0; i < kMaxLaneSteps; ++i) {
            const int rest =
                arpParams_.seqNoteLaneRestFlags[i].load(std::memory_order_relaxed);
            arpCore_.seqRestFlags()[i].store(
                static_cast<uint8_t>(rest != 0 ? 1 : 0),
                std::memory_order_relaxed);
        }

        // Sync lane 9 modulators (speed, swing, jitter, speed-curve depth).
        // These calls are now safe thanks to the T029 audit (each setter
        // accepts laneIndex < kNumLanes == 10).
        arpCore_.setLaneSpeed(9,
            arpParams_.seqNoteLaneSpeed.load(std::memory_order_relaxed));
        arpCore_.setLaneSwing(9,
            arpParams_.seqNoteLaneSwing.load(std::memory_order_relaxed));
        arpCore_.setLaneLengthJitter(9,
            arpParams_.seqNoteLaneJitter.load(std::memory_order_relaxed));
        arpCore_.setLaneSpeedCurveDepth(9,
            arpParams_.seqNoteLaneSpeedCurveDepth.load(std::memory_order_relaxed));
    }
}

} // namespace Gradus
